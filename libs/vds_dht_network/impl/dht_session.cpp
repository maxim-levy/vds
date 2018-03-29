/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/
#include "stdafx.h"
#include "private/dht_session.h"
#include "messages/offer_move_replica.h"
#include "db_model.h"
#include "include/dht_network_client.h"
#include "private/dht_network_client_p.h"
#include "messages/dht_find_node.h"
#include "messages/dht_find_node_response.h"
#include "messages/channel_log_request.h"
#include "messages/channel_log_record.h"

vds::dht::network::dht_session::dht_session(
  const network_address& address,
  const const_data_buffer& this_node_id,
  const const_data_buffer& partner_node_id)
: base_class(address, this_node_id),
  partner_node_id_(partner_node_id){
}

void vds::dht::network::dht_session::ping_node(const service_provider& sp, const const_data_buffer & node_id) {
}

vds::async_task<> vds::dht::network::dht_session::process_message(
  const service_provider& sp,
  uint8_t message_type,
  const const_data_buffer& message_data) {

  switch((network::message_type_t)message_type){
  case network::message_type_t::channel_log_state: {
    return sp.get<db_model>()->async_transaction(sp, [sp, message_data](database_transaction & t) {
      binary_deserializer s(message_data);
      messages::channel_log_state message(s);
      (*sp.get<client>())->apply_message(sp, t, message);
      return true;
    });
    break;
  }
  case network::message_type_t::channel_log_request: {
    return sp.get<db_model>()->async_transaction(sp, [sp, message_data](database_transaction & t) {
      binary_deserializer s(message_data);
      messages::channel_log_request message(s);
      (*sp.get<client>())->apply_message(sp, t, message);
      return true;
    });
    break;
  }
  case network::message_type_t::channel_log_record: {
    return sp.get<db_model>()->async_transaction(sp, [sp, message_data](database_transaction & t) {
      binary_deserializer s(message_data);
      messages::channel_log_record message(s);
      (*sp.get<client>())->apply_message(sp, t, message);
      return true;
    });
    break;
  }
  case network::message_type_t::offer_move_replica: {
      return sp.get<db_model>()->async_transaction(sp, [sp, message_data](database_transaction & t){
        binary_deserializer s(message_data);
        messages::offer_move_replica message(s);
        (*sp.get<client>())->apply_message(sp, t, message);
        return true;
      });
      break;
    }

    case network::message_type_t::dht_find_node: {
      binary_deserializer s(message_data);
      messages::dht_find_node message(s);
      (*sp.get<client>())->apply_message(sp, message);
      break;
    }

    case network::message_type_t::dht_find_node_response: {
      binary_deserializer s(message_data);
      messages::dht_find_node_response message(s);
      (*sp.get<client>())->apply_message(sp, this->shared_from_this(), message);
      break;
    }

    default:{
      throw std::runtime_error("Invalid command");
    }
  }
  return async_task<>::empty();
}
