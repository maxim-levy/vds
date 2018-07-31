/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/
#include "stdafx.h"
#include "private/dht_sync_process.h"
#include "dht_network_client.h"
#include "messages/transaction_log_state.h"
#include "transaction_log_record_dbo.h"
#include "private/dht_network_client_p.h"
#include "chunk_replica_data_dbo.h"
#include "sync_replica_map_dbo.h"
#include "messages/sync_new_election.h"
#include "messages/sync_coronation.h"
#include "sync_state_dbo.h"
#include "sync_member_dbo.h"
#include "messages/sync_leader_broadcast.h"
#include "sync_message_dbo.h"
#include "messages/sync_replica_operations.h"
#include "messages/sync_looking_storage.h"
#include "db_model.h"
#include "device_config_dbo.h"
#include "messages/sync_snapshot.h"
#include "vds_exceptions.h"
#include "sync_local_queue_dbo.h"
#include "messages/sync_add_message.h"


void vds::dht::network::sync_process::do_sync(
  const service_provider& sp,
  database_transaction& t) {

  this->sync_entries(sp, t);
  this->sync_local_queues(sp, t);
}

void vds::dht::network::sync_process::add_local_log(
  const service_provider& sp,
  database_transaction& t,
  const const_data_buffer& object_id,
  orm::sync_message_dbo::message_type_t message_type,
  const const_data_buffer & member_node,
  uint16_t replica,
  const vds::const_data_buffer& leader_node) {

  orm::sync_local_queue_dbo t1;
  auto st = t.get_reader(t1
    .select(t1.last_send)
    .where(t1.object_id == object_id
      && t1.message_type == message_type
      && t1.member_node == member_node
      && t1.replica == replica));
  if(st.execute()) {
    return;
  }

  t.execute(t1.insert(
    t1.object_id = object_id,
    t1.message_type = message_type,
    t1.member_node = member_node,
    t1.replica = replica,
    t1.last_send = std::chrono::system_clock::now()));

  const auto member_index = t.last_insert_rowid();
  auto & client = *sp.get<dht::network::client>();
  if(leader_node == client.current_node_id()) {
    orm::sync_member_dbo t2;
    st = t.get_reader(t2.select(
      t2.generation,
      t2.current_term,
      t2.commit_index,
      t2.last_applied)
    .where(t2.object_id == object_id
      && t2.member_node == client.current_node_id()));

    if(!st.execute()) {
      throw vds_exceptions::invalid_operation();      
    }

    const auto generation = t2.generation.get(st);
    const auto current_term = t2.current_term.get(st);
    auto commit_index = t2.commit_index.get(st);
    const auto last_applied = t2.last_applied.get(st);

    orm::sync_message_dbo t3;
    t.execute(t3.insert(
      t3.object_id = object_id,
      t3.generation = generation,
      t3.current_term = current_term,
      t3.index = last_applied + 1,
      t3.message_type = message_type,
      t3.member_node = member_node,
      t3.replica = replica,
      t3.member_index = member_index));

    t.execute(t2.update(
      t2.last_applied = last_applied + 1)
      .where(t2.object_id == object_id
        && t2.member_node == client.current_node_id()));

    this->send_to_members(
      sp,
      t,
      object_id, 
      messages::sync_replica_operations_request(
        object_id,
        client->current_node_id(),
        generation,
        current_term,
        commit_index,
        last_applied,
        message_type,
        member_node,
        replica));

    while (this->get_quorum(sp, t, object_id) < 2 && commit_index < last_applied) {
      this->apply_record(sp, t, object_id, generation, current_term, commit_index);
      ++commit_index;
    }
  }
  else {
    client->send(
      sp,
      leader_node,
      messages::sync_add_message_request(
        object_id,
        leader_node,
        client->current_node_id(),
        member_index,
        message_type,
        member_node,
        replica));
  }
}

std::set<vds::const_data_buffer> vds::dht::network::sync_process::get_members(
  const service_provider& sp,
  database_read_transaction& t,
  const vds::const_data_buffer& object_id) {

  orm::sync_member_dbo t1;
  auto st = t.get_reader(t1.select(t1.member_node).where(t1.object_id == object_id));

  std::set<vds::const_data_buffer> result;
  while(st.execute()) {
    result.emplace(t1.member_node.get(st));
  }

  return result;
}

void vds::dht::network::sync_process::make_new_election(
  const service_provider & sp,
  database_transaction & t,
  const const_data_buffer & object_id) const {
  auto & client = *sp.get<dht::network::client>();

  orm::sync_state_dbo t1;
  orm::sync_member_dbo t2;
  auto st = t.get_reader(t1.select(
    t2.generation, t2.current_term)
    .inner_join(t2, t2.object_id == t1.object_id && t2.member_node == client->current_node_id())
    .where(t1.object_id == object_id));

  if (st.execute()) {
    const auto generation = t2.generation.get(st);
    const auto current_term = t2.current_term.get(st);

    t.execute(t1.update(
      t1.state = orm::sync_state_dbo::state_t::canditate,
      t1.next_sync = std::chrono::system_clock::now() + ELECTION_TIMEOUT)
      .where(t1.object_id == object_id));

    t.execute(t2.update(
      t2.voted_for = client->current_node_id(),
      t2.current_term = current_term + 1,
      t2.commit_index = 0,
      t2.last_applied = 0,
      t2.last_activity = std::chrono::system_clock::now())
      .where(t2.object_id == object_id && t2.member_node == client->current_node_id()));

    auto members = this->get_members(sp, t, object_id);
    for (const auto & member : members) {
      client->send(
        sp,
        member,
        messages::sync_new_election_request(
          object_id,
          generation,
          current_term,
          member,
          client->current_node_id()));
    }
  }
  else {
    throw vds_exceptions::not_found();
  }
}

vds::dht::network::sync_process::base_message_type vds::dht::network::sync_process::apply_base_message(
  const service_provider& sp,
  database_transaction& t,
  const messages::sync_base_message_request & message) {

  auto & client = *sp.get<dht::network::client>();

  orm::sync_state_dbo t1;
  orm::sync_member_dbo t2;
  auto st = t.get_reader(t1.select(
    t1.state, t2.generation, t2.current_term, t2.voted_for, t2.last_applied, t2.commit_index)
    .inner_join(t2, t2.object_id == t1.object_id && t2.member_node == client->current_node_id())
    .where(t1.object_id == message.object_id()));

  if (st.execute()) {
    if (
      message.generation() > t2.generation.get(st)
      || (message.generation() == t2.generation.get(st) && message.current_term() > t2.current_term.get(st))) {
      send_snapshot_request(sp, message.object_id(), message.leader_node());
      return base_message_type::from_future;
    }
    else if (
      message.generation() < t2.generation.get(st)
      || (message.generation() == t2.generation.get(st) && message.current_term() < t2.current_term.get(st))) {

      const auto leader = this->get_leader(sp, t, message.object_id());
      if(!leader || client->current_node_id() == leader) {
        send_snapshot(sp, t, message.object_id(), { message.source_node() });
      }
      else {
        send_snapshot_request(sp, message.object_id(), message.leader_node(), message.source_node());
      }

      return base_message_type::from_past;
    }
    else if(
      message.generation() == t2.generation.get(st)
      && t2.voted_for.get(st) != message.leader_node()) {
        this->make_new_election(sp, t, message.object_id());
      return base_message_type::other_leader;
    }
    else if(message.generation() == t2.generation.get(st)
      && message.current_term() == t2.current_term.get(st)) {
      return base_message_type::successful;
    }
    else {
      throw std::runtime_error("Case error");
    }
  }
  else {
    return base_message_type::not_found;
  }
}

uint32_t vds::dht::network::sync_process::get_quorum(
  const service_provider& sp,
  database_read_transaction& t,
  const const_data_buffer& object_id) const {

  db_value<uint64_t> member_count;
  orm::sync_member_dbo t1;
  auto st = t.get_reader(t1.select(
    db_count(t1.member_node).as(member_count))
    .where(t1.object_id == object_id));

  if(st.execute()) {
    return member_count.get(st) / 2 + 1;
  }

  return 0;
}

bool vds::dht::network::sync_process::apply_base_message(
  const service_provider& sp,
  database_transaction& t,
  const messages::sync_base_message_response & message) {

  auto & client = *sp.get<dht::network::client>();

  orm::sync_state_dbo t1;
  orm::sync_member_dbo t2;
  auto st = t.get_reader(t1.select(
    t1.state, t2.generation, t2.current_term, t2.voted_for, t2.last_applied, t2.commit_index)
    .inner_join(t2, t2.object_id == t1.object_id && t2.member_node == client->current_node_id())
    .where(t1.object_id == message.object_id()));

  if (st.execute()) {
    if (
      message.generation() > t2.generation.get(st)
      || (message.generation() == t2.generation.get(st) && message.current_term() > t2.current_term.get(st))) {
      send_snapshot_request(sp, message.object_id(), message.leader_node());
      return false;
    }
    else if (
      message.generation() < t2.generation.get(st)
      || (message.generation() == t2.generation.get(st) && message.current_term() < t2.current_term.get(st))) {

      const auto leader = this->get_leader(sp, t, message.object_id());
      if (!leader || client->current_node_id() == leader) {
        send_snapshot(sp, t, message.object_id(), { message.source_node() });
      }
      else {
        send_snapshot_request(sp, message.object_id(), message.leader_node(), message.source_node());
      }

      return false;
    }
    else if (
      message.generation() == t2.generation.get(st)
      && t2.voted_for.get(st) != message.leader_node()) {
      this->make_new_election(sp, t, message.object_id());
      return false;
    }
    else if (message.generation() == t2.generation.get(st)
      && message.current_term() == t2.current_term.get(st)) {

      if(t1.state.get(st) == orm::sync_state_dbo::state_t::leader) {
        const auto generation = t2.generation.get(st);
        const auto current_term = t2.current_term.get(st);
        auto last_applied = t2.last_applied.get(st);
        const auto commit_index = t2.commit_index.get(st);

        st = t.get_reader(t2.select(
          t2.generation,
          t2.current_term,
          t2.commit_index,
          t2.last_applied)
          .where(t2.object_id == message.object_id() && t2.member_node == message.source_node()));
        if(st.execute()) {
          if (t2.last_applied.get(st) < message.last_applied()) {
            t.execute(t2.update(
              t2.generation = message.generation(),
              t2.current_term = message.current_term(),
              t2.commit_index = message.commit_index(),
              t2.last_applied = message.last_applied(),
              t2.last_activity = std::chrono::system_clock::now())
              .where(t2.object_id == message.object_id() && t2.member_node == message.source_node()));

            const auto quorum = this->get_quorum(sp, t, message.object_id());
            for (;;) {
              db_value<uint64_t> applied_count;
              st = t.get_reader(t2.select(
                db_count(t2.member_node).as(applied_count))
                .where(t2.object_id == message.object_id()
                  && t2.generation == generation
                  && t2.current_term == current_term
                  && t2.last_applied == last_applied + 1));
              if(applied_count.get(st) >= quorum) {
                this->apply_record(sp, t, message.object_id(), generation, current_term, ++last_applied);
              }
              else {
                break;
              }
            }
          }
        }
      }
    }
  }
  else {
    return false;
  }

  return true;
}

void vds::dht::network::sync_process::add_sync_entry(
    const service_provider &sp,
    database_transaction &t,
    const const_data_buffer &object_id,
    uint32_t object_size) {

  const_data_buffer leader;
  auto & client = *sp.get<dht::network::client>();

  orm::sync_state_dbo t1;
  orm::sync_member_dbo t2;
  auto st = t.get_reader(t1.select(t1.state, t2.voted_for)
    .inner_join(t2, t2.object_id == t1.object_id && t2.member_node == client.current_node_id())
    .where(t1.object_id == object_id));
  if(!st.execute()) {
    leader = client.current_node_id();

    t.execute(t1.insert(
      t1.object_id = object_id,
      t1.object_size = object_size,
      t1.state = orm::sync_state_dbo::state_t::leader,
      t1.next_sync = std::chrono::system_clock::now() + LEADER_BROADCAST_TIMEOUT));

    t.execute(t2.insert(
      t2.object_id = object_id,
      t2.member_node = client->current_node_id(),
      t2.last_activity = std::chrono::system_clock::now(),
      t2.voted_for = client.current_node_id(),
      t2.generation = 0,
      t2.current_term = 0,
      t2.commit_index = 0,
      t2.last_applied = 0));

    client->send_near(
      sp,
      object_id,
      _client::GENERATE_DISTRIBUTED_PIECES,
      messages::sync_looking_storage_request(
        object_id,
        client.current_node_id(),
        0,
        0,
        0,
        0,
        object_size));
  }
  else {
    leader = t2.voted_for.get(st);
  }

  sp.get<logger>()->trace(ThisModule, sp, "Make leader %s:0:0", base64::from_bytes(object_id).c_str());

  orm::sync_replica_map_dbo t3;
  for(uint16_t i = 0; i < _client::GENERATE_DISTRIBUTED_PIECES; ++i) {
    this->add_local_log(
      sp,
      t,
      object_id,
      orm::sync_message_dbo::message_type_t::add_replica,
      client->current_node_id(),
      i,
      leader);
  }
}

void vds::dht::network::sync_process::apply_message(
  const service_provider& sp,
  database_transaction & t,
  const messages::sync_looking_storage_request & message) {

  auto & client = *sp.get<dht::network::client>();
  client->send_closer(
    sp,
    message.object_id(),
    _client::GENERATE_DISTRIBUTED_PIECES,
    message);

  switch (this->apply_base_message(sp, t, message)) {
  case base_message_type::not_found:
  case base_message_type::successful:
    break;

  case base_message_type::from_future:
  case base_message_type::from_past:
  case base_message_type::other_leader:
    return;

  default:
    throw vds_exceptions::invalid_operation();
  }

  for (const auto & record : orm::device_config_dbo::get_free_space(t, client.current_node_id())) {
    if (record.used_size + message.object_size() < record.reserved_size
      && message.object_size() < record.free_size) {

      std::set<uint16_t> replicas;
      orm::chunk_replica_data_dbo t1;
      auto st = t.get_reader(t1.select(t1.replica).where(t1.object_id == message.object_id()));
      while (st.execute()) {
        replicas.emplace(t1.replica.get(st));
      }

      client->send(
        sp,
        message.leader_node(),
        messages::sync_looking_storage_response(
          message.object_id(),
          message.leader_node(),
          client.current_node_id(),
          replicas));

      return;
    }
  }
}

void vds::dht::network::sync_process::apply_message(
    const service_provider& sp,
    database_transaction & t,
    const messages::sync_looking_storage_response & message) {

  auto & client = *sp.get<dht::network::client>();
  if (message.leader_node() != client.current_node_id()) {
    client->send(
        sp,
        message.leader_node(),
        message);
    return;
  }

  orm::sync_state_dbo t1;
  orm::sync_member_dbo t2;
  auto st = t.get_reader(
      t1.select(
              t1.state,
              t2.generation,
              t2.current_term,
              t2.commit_index,
              t2.last_applied,
              t2.voted_for)
    .inner_join(t2, t2.object_id == t1.object_id && t2.member_node == client->current_node_id())
          .where(t1.object_id == message.object_id()));

  if(!st.execute()) {
    return;
  }

  if (orm::sync_state_dbo::state_t::leader != static_cast<orm::sync_state_dbo::state_t>(t1.state.get(st))) {
    const auto leader = t2.voted_for.get(st);
    client->send(
        sp,
        leader,
        messages::sync_looking_storage_response(
            message.object_id(),
            leader,
            message.source_node(),
            message.replicas()));

    return;
  }

  const auto generation = t2.generation.get(st);
  const auto current_term = t2.current_term.get(st);
  const auto commit_index = t2.commit_index.get(st);
  auto index = t2.last_applied.get(st);

  st = t.get_reader(t2.select(t2.generation).where(t2.object_id == message.object_id() && t2.member_node == message.source_node()));
  if(!st.execute()) {
    this->add_local_log(
      sp,
      t,
      message.object_id(),
      orm::sync_message_dbo::message_type_t::add_member,
      message.source_node(),
      0,
      client->current_node_id());
  }

  if (!message.replicas().empty()) {
    //Register replica
    for (auto replica : message.replicas()) {
      this->add_local_log(
        sp,
        t,
        message.object_id(),
        orm::sync_message_dbo::message_type_t::add_replica,
        message.source_node(),
        replica,
        client->current_node_id());
    }
  }
}

void vds::dht::network::sync_process::apply_message(const service_provider& sp, database_transaction& t,
  const messages::sync_snapshot_request& message) {
  auto & client = *sp.get<dht::network::client>();
  const auto leader = this->get_leader(sp, t, message.object_id());
  if (client->current_node_id() != leader) {
    client->send(
      sp,
      leader,
      message);
    return;
  }

  send_snapshot(sp, t, message.object_id(), { message.source_node() });
}

void vds::dht::network::sync_process::apply_message(
  const service_provider& sp,
  database_transaction& t,
  const messages::sync_snapshot_response& message) {

  auto & client = *sp.get<dht::network::client>();

  orm::sync_state_dbo t1;
  orm::sync_member_dbo t2;
  auto st = t.get_reader(t1.select(
    t1.state, t2.generation, t2.current_term, t2.voted_for, t2.last_applied, t2.commit_index)
    .inner_join(t2, t2.object_id == t1.object_id && t2.member_node == client->current_node_id())
    .where(t1.object_id == message.object_id()));

  if (st.execute()) {
    const auto state = t1.state.get(st);
    const auto generation = t2.generation.get(st);
    const auto current_term = t2.current_term.get(st);
    const auto voted_for = t2.voted_for.get(st);
    const auto last_applied = t2.last_applied.get(st);
    const auto commit_index = t2.commit_index.get(st);

    if (
      message.generation() < generation
      || (message.generation() == generation && message.current_term() < current_term)) {
      if (state == orm::sync_state_dbo::state_t::leader) {
        send_snapshot(sp, t, message.object_id(), { message.leader_node() });
      }
      else {
        send_snapshot_request(sp, message.object_id(), voted_for, message.leader_node());
      }
      return;
    }

    if(last_applied > message.last_applied()) {
      return;
    }

    //merge
    t.execute(t1.update(
      t1.state = orm::sync_state_dbo::state_t::follower,
      t1.next_sync = std::chrono::system_clock::now() + LEADER_BROADCAST_TIMEOUT)
    .where(t1.object_id == message.object_id()));

    auto members = this->get_members(sp, t, message.object_id());
    for (const auto & member : message.members()) {
      auto p = members.find(member.first);
      if (members.end() == p) {
        t.execute(t2.insert(
          t2.object_id = message.object_id(),
          t2.member_node = member.first,
          t2.voted_for = message.leader_node(),
          t2.generation = message.generation(),
          t2.current_term = message.current_term(),
          t2.commit_index = message.commit_index(),
          t2.last_applied = message.last_applied(),
          t2.last_activity = std::chrono::system_clock::now()));
      }
      else {
        members.erase(p);
      }
    }

    for (const auto & member : members) {
      t.execute(t2.delete_if(
        t2.object_id == message.object_id()
        && t2.member_node == member));
    }
  }
  else if(message.members().end() != message.members().find(client->current_node_id())) {
    t.execute(t1.insert(
    t1.object_id = message.object_id(),
      t1.object_size = message.object_size(),
      t1.state = orm::sync_state_dbo::state_t::follower,
      t1.next_sync = std::chrono::system_clock::now() + LEADER_BROADCAST_TIMEOUT));

    for (const auto & member : message.members()) {
      t.execute(t2.insert(
        t2.object_id = message.object_id(),
        t2.member_node = member.first,
        t2.voted_for = message.leader_node(),
        t2.generation = message.generation(),
        t2.current_term = message.current_term(),
        t2.commit_index = message.commit_index(),
        t2.last_applied = message.last_applied(),
        t2.last_activity = std::chrono::system_clock::now()));
    }
  }
  else {
    return;
  }
  orm::sync_replica_map_dbo t3;
  t.execute(t3.delete_if(t3.object_id == message.object_id()));

  for (const auto & node : message.replica_map()) {
    for (const auto & replica : node.second) {
      t.execute(t3.insert(
        t3.object_id = message.object_id(),
        t3.replica = replica,
        t3.node = node.first,
        t3.last_access = std::chrono::system_clock::now()));
    }
  }
}

void vds::dht::network::sync_process::apply_message(
  const service_provider& sp,
  database_transaction& t,
  const messages::sync_add_message_request& message) {
}

void vds::dht::network::sync_process::apply_message(const service_provider& sp, database_transaction& t,
  const messages::sync_leader_broadcast_request& message) {
}

void vds::dht::network::sync_process::apply_message(const service_provider& sp, database_transaction& t,
  const messages::sync_leader_broadcast_response& message) {
}

void vds::dht::network::sync_process::apply_message(const service_provider& sp, database_transaction& t,
  const messages::sync_replica_operations_request& message) {
}

void vds::dht::network::sync_process::apply_message(const service_provider& sp, database_transaction& t,
  const messages::sync_replica_operations_response& message) {
}

void vds::dht::network::sync_process::send_leader_broadcast(
  const vds::service_provider& sp,
  vds::database_transaction& t,
  const const_data_buffer & object_id) {

  auto & client = *sp.get<dht::network::client>();
  orm::sync_state_dbo t1;
  orm::sync_member_dbo t2;
  auto st = t.get_reader(
    t2.select(t2.member_node, t2.last_activity)
      .where(t2.object_id == object_id));
  std::set<const_data_buffer> to_remove;
  std::set<const_data_buffer> member_nodes;
  while (st.execute()) {
    const auto member_node = t2.member_node.get(st);
    const auto last_activity = t2.last_activity.get(st);

    if(std::chrono::system_clock::now() - last_activity > MEMBER_TIMEOUT) {
      to_remove.emplace(member_node);
    }
    else {
      member_nodes.emplace(member_node);
    }
  }

  if(to_remove.empty()) {
    st = t.get_reader(
      t2.select(t2.generation, t2.current_term, t2.commit_index, t2.last_applied)
      .where(t2.object_id == object_id && t2.member_node == client->current_node_id()));
    if(!st.execute()) {
      throw vds_exceptions::invalid_operation();
    }

    const auto generation = t2.generation.get(st);
    const auto current_term = t2.current_term.get(st);
    const auto commit_index = t2.commit_index.get(st);
    const auto last_applied = t2.last_applied.get(st);

    for (const auto & member_node : member_nodes) {
      sp.get<logger>()->trace(ThisModule, sp, "Send leader broadcast to %s", base64::from_bytes(member_node).c_str());

      client->send(
        sp,
        member_node,
        messages::sync_leader_broadcast_request(
          object_id,
          client.client::current_node_id(),
          generation,
          current_term,
          commit_index,
          last_applied));
    }
  }
  else {
    //Remove members
    for (const auto & member_node : to_remove) {
      this->add_local_log(
        sp,
        t,
        object_id,
        orm::sync_message_dbo::message_type_t::remove_member,
        member_node,
        0,
        client->current_node_id());
    }
  }

  if(_client::GENERATE_DISTRIBUTED_PIECES > member_nodes.size()) {
    st = t.get_reader(
      t1.select(t1.object_size, t2.generation, t2.current_term, t2.commit_index, t2.last_applied)
      .inner_join(t2, t2.object_id == t1.object_id && t2.member_node == client->current_node_id())
      .where(t1.object_id == object_id));
    if (!st.execute()) {
      throw vds_exceptions::invalid_operation();
    }

    const auto generation = t2.generation.get(st);
    const auto current_term = t2.current_term.get(st);
    const auto commit_index = t2.commit_index.get(st);
    const auto last_applied = t2.last_applied.get(st);
    const auto object_size = t1.object_size.get(st);

    client->send_near(
      sp,
      object_id,
      _client::GENERATE_DISTRIBUTED_PIECES,
      messages::sync_looking_storage_request(
        object_id,
        client.current_node_id(),
        generation,
        current_term,
        commit_index,
        last_applied,
        object_size),
      [&member_nodes](const dht_route<std::shared_ptr<dht_session>>::node & node)->bool {
        return member_nodes.end() == member_nodes.find(node.node_id_);
      });
  }

  t.execute(
    t1.update(
        t1.next_sync = std::chrono::system_clock::now() + LEADER_BROADCAST_TIMEOUT)
      .where(t1.object_id == object_id));
}

void vds::dht::network::sync_process::sync_entries(
  const service_provider& sp,
  database_transaction& t) {

  auto & client = *sp.get<dht::network::client>();

  std::map<const_data_buffer, orm::sync_state_dbo::state_t> objects;
  orm::sync_state_dbo t1;
  auto st = t.get_reader(
    t1.select(
      t1.object_id,
      t1.state)
    .where(t1.next_sync <= std::chrono::system_clock::now()));
  while (st.execute()) {
    const auto object_id = t1.object_id.get(st);
    objects[object_id] = t1.state.get(st);
  }

  for (auto & p : objects) {
    switch (p.second) {

    case orm::sync_state_dbo::state_t::follower: {
      this->make_new_election(sp, t, p.first);
      break;
    }

    case orm::sync_state_dbo::state_t::canditate: {
      this->make_leader(sp, t, p.first);
      break;
    }

    case orm::sync_state_dbo::state_t::leader: {
      this->send_leader_broadcast(sp, t, p.first);
      break;
    }

    }
  }
}

void vds::dht::network::sync_process::send_snapshot_request(
  const service_provider& sp,
  const const_data_buffer& object_id,
  const const_data_buffer& leader_node,
  const const_data_buffer& from_node) {

  auto & client = *sp.get<dht::network::client>();
  client->send(
    sp,
    leader_node,
    messages::sync_snapshot_request(
      object_id,
      ((!from_node) ? client.current_node_id() : from_node)));

}

void vds::dht::network::sync_process::apply_message(
  const service_provider& sp,
  database_transaction& t,
  const messages::sync_new_election_request & message) {

  auto & client = *sp.get<dht::network::client>();
  vds_assert(message.target_node() == client->current_node_id());

  orm::sync_state_dbo t1;
  orm::sync_member_dbo t2;
  auto st = t.get_reader(t1.select(
    t1.state, t2.voted_for, t2.generation, t2.current_term)
    .inner_join(t2, t2.object_id == t1.object_id && t2.member_node == client->current_node_id())
    .where(t1.object_id == message.object_id()));

  if (st.execute()) {
    if (t2.generation.get(st) < message.generation()
      || (t2.generation.get(st) == message.generation() && t2.current_term.get(st) < message.current_term())) {
      this->make_follower(sp, t, message.object_id(), message.generation(), message.current_term(), message.source_node());
      return;
    }
    if (t2.generation.get(st) > message.generation()
      || (t2.generation.get(st) == message.generation() && t2.current_term.get(st) > message.current_term())) {
      if (t1.state.get(st) == orm::sync_state_dbo::state_t::leader) {
        send_snapshot(sp, t, message.object_id(), { message.source_node() });
      }
      else {
        this->send_snapshot_request(sp, message.object_id(), t2.voted_for.get(st), message.source_node());
      }
      return;
    }

    vds_assert(t2.generation.get(st) == message.generation() && t2.current_term.get(st) == message.current_term());
  }
  else {
    this->send_snapshot_request(sp, message.object_id(), message.source_node(), client->current_node_id());
  }
}

void vds::dht::network::sync_process::apply_message(
  const service_provider& sp,
  database_transaction& t,
  const messages::sync_new_election_response & message) {

  auto & client = *sp.get<dht::network::client>();
  vds_assert(message.target_node() == client->current_node_id());

  orm::sync_state_dbo t1;
  orm::sync_member_dbo t2;
  auto st = t.get_reader(t1.select(
    t1.state, t2.voted_for, t2.generation, t2.current_term)
    .inner_join(t2, t2.object_id == t1.object_id && t2.member_node == client->current_node_id())
    .where(t1.object_id == message.object_id()));

  if (st.execute()
    && t1.state.get(st) == orm::sync_state_dbo::state_t::canditate
    && t2.generation.get(st) == message.generation()
    && t2.current_term.get(st) == message.current_term()) {

    st = t.get_reader(t2.select(t2.last_activity).where(
      t2.object_id == message.object_id()
      && t2.member_node == message.source_node()));
    if(!st.execute()) {
      t.execute(t2.insert(
        t2.object_id = message.object_id(),
        t2.member_node = message.source_node(),
        t2.voted_for = client->current_node_id(),
        t2.generation = message.generation(),
        t2.current_term = message.current_term(),
        t2.commit_index = 0,
        t2.last_applied = 0,
        t2.last_activity = std::chrono::system_clock::now()));
    }
    else {
      t.execute(t2.update(
        t2.voted_for = client->current_node_id(),
        t2.generation = message.generation(),
        t2.current_term = message.current_term(),
        t2.commit_index = 0,
        t2.last_applied = 0,
        t2.last_activity = std::chrono::system_clock::now())
        .where(t2.object_id == message.object_id() && t2.member_node == message.source_node()));
    }

    db_value<uint64_t> voted_count;
    st = t.get_reader(
      t2.select(
        db_count(t2.member_node).as(voted_count))
      .where(t2.object_id == message.object_id()
        && t2.voted_for == client->current_node_id()
        && t2.generation == message.generation()
        && t2.current_term == message.current_term()));
    if(!st.execute()) {
      throw vds_exceptions::invalid_operation();
    }

    const auto count = voted_count.get(st);
    if(count >= this->get_quorum(sp, t, message.object_id())) {
      this->make_leader(sp, t, message.object_id());
    }
  }
}


//void vds::dht::network::sync_process::apply_message(
//  const service_provider& sp,
//  database_transaction & t,
//  const messages::sync_coronation_request& message) {
//
//  orm::sync_state_dbo t1;
//  orm::sync_member_dbo t2;
//  auto st = t.get_reader(t1.select(
//    t1.state, t1.generation, t1.current_term)
//    .where(t1.object_id == base64::from_bytes(message.object_id())));
//
//  if(st.execute()) {
//    if (
//      message.generation() < t1.generation.get(st)
//      || (message.generation() == t1.generation.get(st) && message.current_term() < t1.current_term.get(st))) {
//      this->send_coronation_request(sp, t, message.object_id(), message.source_node());
//    }
//    else {
//      auto & client = *sp.get<dht::network::client>();
//      if (message.member_notes().end() == message.member_notes().find(client->current_node_id())) {
//        client->send(
//          sp,
//          message.source_node(),
//          messages::sync_member_operation_request(
//            message.object_id(),
//            client.current_node_id(),
//            t1.generation.get(st),
//            t1.current_term.get(st),
//            messages::sync_member_operation_request::operation_type_t::add_member));
//
//        t.execute(t2.delete_if(t2.object_id == message.object_id()));
//        t.execute(t1.delete_if(t1.object_id == message.object_id()));
//      } else {
//        this->make_follower(sp, t, message);
//      }
//    }
//  }
//  else {
//    this->make_follower(sp, t, message);
//  }
//
//  this->sync_object_->schedule(sp, [this, sp, message]() {
//    auto p = this->sync_entries_.find(message.object_id());
//    if (this->sync_entries_.end() == p) {
//      auto & entry = this->sync_entries_.at(message.object_id());
//      entry.make_follower(sp, message.object_id(), message.source_node(), message.current_term());
//
//      auto & client = *sp.get<dht::network::client>();
//      if (message.member_notes().end() == message.member_notes().find(client->current_node_id())) {
//        client->send(
//          sp,
//          message.source_node(),
//          messages::sync_coronation_response(
//            message.object_id(),
//            message.current_term(),
//            client.current_node_id()));
//      }
//    }
//    else if (p->second.current_term_ <= message.current_term()) {
//      p->second.make_follower(sp, message.object_id(), message.source_node(), message.current_term());
//
//      auto & client = *sp.get<dht::network::client>();
//      if (message.member_notes().end() == message.member_notes().find(client->current_node_id())) {
//        client->send(
//          sp,
//          message.source_node(),
//          messages::sync_coronation_response(
//            message.object_id(),
//            message.current_term(),
//            client.current_node_id()));
//      }
//    }
//    else {
//      auto & client = *sp.get<dht::network::client>();
//      if (message.member_notes().end() == message.member_notes().find(client->current_node_id())) {
//        client->send(
//          sp,
//          message.source_node(),
//          messages::sync_coronation_request(
//            message.object_id(),
//            message.current_term(),
//            std::set<const_data_buffer>(),
//            p->second.voted_for_));
//      }
//    }
//  });
//}
//
//void vds::dht::network::sync_process::apply_message(
//  const service_provider& sp,
//  const messages::sync_coronation_response& message) {
//
//}

vds::const_data_buffer
vds::dht::network::sync_process::get_leader(
    const vds::service_provider &sp,
    vds::database_transaction &t,
    const const_data_buffer &object_id) {
  auto client = sp.get<dht::network::client>();
  orm::sync_state_dbo t1;
  orm::sync_member_dbo t2;
  auto st = t.get_reader(
      t1.select(
              t1.state,
              t2.voted_for)
    .inner_join(t2, t2.object_id == t1.object_id && t2.member_node == client->current_node_id())
          .where(t1.object_id == object_id));
  if(st.execute()) {
    if(orm::sync_state_dbo::state_t::leader == static_cast<orm::sync_state_dbo::state_t>(t1.state.get(st))){
      return client->current_node_id();
    }
    else {
      return t2.voted_for.get(st);
    }
  }

  return const_data_buffer();
}

void vds::dht::network::sync_process::apply_record(const vds::service_provider& sp, vds::database_transaction& t,
  const const_data_buffer& object_id, uint64_t generation, uint64_t current_term, uint64_t message_index) {
  orm::sync_message_dbo t1;
  auto st = t.get_reader(
    t1.select(
      t1.message_type,
      t1.replica,
      t1.member_node)
    .where(t1.object_id == object_id
      && t1.generation == generation
      && t1.current_term == current_term
      && t1.index == message_index));
  if(!st.execute()) {
    throw vds_exceptions::not_found();
  }

  this->apply_record(
    sp,
    t,
    object_id,
    t1.message_type.get(st),
    t1.member_node.get(st),
    t1.replica.get(st),
    message_index);

  const auto client = sp.get<dht::network::client>();
  orm::sync_member_dbo t2;
  t.execute(t2.update(
    t2.commit_index = message_index)
    .where(
      t2.object_id == object_id
      && t2.member_node == client->current_node_id()
      && t2.generation == generation
      && t2.current_term == current_term
      && t2.commit_index == message_index - 1));
  vds_assert(1 == t.rows_modified());
}

void vds::dht::network::sync_process::apply_record(
    const vds::service_provider &sp,
    vds::database_transaction &t,
    const const_data_buffer &object_id,
    orm::sync_message_dbo::message_type_t message_type,
    const const_data_buffer & member_node,
    uint16_t replica,
    uint64_t message_index) {

  switch (message_type) {
    case orm::sync_message_dbo::message_type_t::add_member: {
      orm::sync_member_dbo t1;
      auto st = t.get_reader(
          t1.select(t1.object_id)
              .where(
                  t1.object_id == object_id
                  && t1.member_node == member_node));

      if (!st.execute()) {
        t.execute(
            t1.insert(
                t1.object_id = object_id,
                t1.member_node = member_node,
                t1.last_activity = std::chrono::system_clock::now()));
      }

      this->send_snapshot(sp, t, object_id, { member_node });
      break;
    }

    case orm::sync_message_dbo::message_type_t::remove_member: {
      orm::sync_member_dbo t1;
      t.execute(
        t1.delete_if(
          t1.object_id == object_id
          && t1.member_node == member_node));

      break;
    }

    case orm::sync_message_dbo::message_type_t::add_replica:{
      orm::sync_replica_map_dbo t1;
      auto st = t.get_reader(
          t1.select(t1.last_access)
              .where(
                  t1.object_id == object_id
                  && t1.node == member_node
                  && t1.replica == replica));
      if (!st.execute()) {
        t.execute(
            t1.insert(
                t1.object_id = object_id,
                t1.node = member_node,
                t1.replica = replica,
                t1.last_access = std::chrono::system_clock::now()));
      }

      break;
    }

    case orm::sync_message_dbo::message_type_t::remove_replica: {
      orm::sync_replica_map_dbo t1;
      t.execute(
        t1.delete_if(
          t1.object_id == object_id
          && t1.node == member_node
          && t1.replica == replica));

      break;
    }

    default:{
      throw std::runtime_error("Invalid operation");
    }
  }

  //remove local record
  const auto client = sp.get<dht::network::client>();
  if(client->current_node_id() == member_node) {
    orm::sync_local_queue_dbo t2;
    t.execute(t2.delete_if(t2.object_id == object_id && t2.local_index == message_index));
    vds_assert(t.rows_modified() == 1);
  }
}

void vds::dht::network::sync_process::send_to_members(const service_provider& sp, database_read_transaction& t,
  const const_data_buffer& object_id, dht::network::message_type_t message_id, const const_data_buffer& message_body) const {
  const auto client = sp.get<dht::network::client>();
  auto members = this->get_members(sp, t, object_id);
  for(const auto & member : members) {
    (*client)->send(sp, member, message_id, message_body);
  }
}

void vds::dht::network::sync_process::send_snapshot(
  const service_provider& sp,
  database_read_transaction& t,
  const const_data_buffer& object_id,
  const std::set<vds::const_data_buffer> & target_nodes) {

  const auto client = sp.get<dht::network::client>();

  orm::sync_state_dbo t1;
  orm::sync_member_dbo t2;
  auto st = t.get_reader(
    t1.select(
      t1.object_size,
      t1.state,
      t2.generation,
      t2.current_term,
      t2.commit_index,
      t2.last_applied)
    .inner_join(t2, t2.object_id == t1.object_id && t2.member_node == client->current_node_id())
    .where(t1.object_id == object_id));

  if(!st.execute() || orm::sync_state_dbo::state_t::leader != t1.state.get(st)) {
    return;
  }

  const auto object_size = t1.object_size.get(st);
  const auto generation = t2.generation.get(st);
  const auto current_term = t2.current_term.get(st);
  const auto commit_index = t2.commit_index.get(st);
  const auto last_applied = t2.last_applied.get(st);

  //
  orm::sync_replica_map_dbo t3;
  st = t.get_reader(
    t3.select(
      t3.replica,
      t3.node)
    .where(t3.object_id == object_id));
  std::map<const_data_buffer, std::set<uint16_t>> replica_map;
  while(st.execute()) {
    replica_map[t3.node.get(st)].emplace(t3.replica.get(st));
  }

  //
  st = t.get_reader(
    t2.select(
      t2.member_node,
      t2.voted_for)
    .where(t2.object_id == object_id));
  std::map<const_data_buffer, messages::sync_snapshot_response::member_state> members;
  while (st.execute()) {
    auto & p = members.at(t2.member_node.get(st));
    p.voted_for = t2.voted_for.get(st);
  }

  for (const auto & target_node : target_nodes) {
    (*client)->send(
      sp,
      target_node,
      messages::sync_snapshot_response(
        object_id,
        object_size,
        target_node,
        client->current_node_id(),
        generation,
        current_term,
        commit_index,
        last_applied,
        replica_map,
        members));
  }
}

void vds::dht::network::sync_process::sync_local_queues(
  const service_provider& sp,
  database_transaction& t) {
  const auto client = sp.get<dht::network::client>();

  orm::sync_local_queue_dbo t1;
  orm::sync_state_dbo t2;
  orm::sync_member_dbo t3;
  auto st = t.get_reader(t1.select(
    t1.local_index,
    t1.object_id,
    t1.message_type,
    t1.member_node,
    t1.replica,
    t3.voted_for)
    .inner_join(t2, t2.state == orm::sync_state_dbo::state_t::follower && t2.object_id == t1.object_id)
    .inner_join(t3, t3.object_id == t1.object_id && t3.member_node == client->current_node_id())
    .where(t1.last_send <= std::chrono::system_clock::now() - LOCAL_QUEUE_TIMEOUT));
  std::set<uint64_t> processed;
  while (st.execute()) {
    (*client)->send(
      sp,
      t3.voted_for.get(st),
      messages::sync_add_message_request(
        t1.object_id.get(st),
        t3.voted_for.get(st),
        client->current_node_id(),
        t1.local_index.get(st),
        t1.message_type.get(st),
        t1.member_node.get(st),
        t1.replica.get(st)));
    processed.emplace(t1.local_index.get(st));
  }

  for(const auto local_index : processed) {
    t.execute(t1.update(t1.last_send = std::chrono::system_clock::now()).where(t1.local_index == local_index));
  }
}

void vds::dht::network::sync_process::make_leader(
  const service_provider& sp,
  database_transaction& t,
  const const_data_buffer& object_id) {
  const auto client = sp.get<dht::network::client>();

  orm::sync_state_dbo t1;
  t.execute(t1.update(
    t1.state = orm::sync_state_dbo::state_t::leader,
    t1.next_sync = std::chrono::system_clock::now() + FOLLOWER_TIMEOUT)
    .where(t1.object_id == object_id));

  this->send_snapshot(sp, t, object_id, this->get_members(sp, t, object_id));
}

void vds::dht::network::sync_process::make_follower(const service_provider& sp, database_transaction& t,
  const const_data_buffer& object_id, uint64_t generation, uint64_t current_term,
  const const_data_buffer& leader_node) {

  orm::sync_state_dbo t1;
  t.execute(t1.update(
    t1.state = orm::sync_state_dbo::state_t::follower,
    t1.next_sync = std::chrono::system_clock::now() + LEADER_BROADCAST_TIMEOUT)
    .where(t1.object_id == object_id));

  const auto client = sp.get<dht::network::client>();

  orm::sync_member_dbo t2;
  t.execute(t2.update(
    t2.generation = generation,
    t2.current_term = current_term,
    t2.commit_index = 0,
    t2.last_applied = 0,
    t2.last_activity = std::chrono::system_clock::now())
  .where(t2.object_id == object_id && t2.member_node == client->current_node_id()));
}
