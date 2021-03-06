/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/
#include "stdafx.h"
#include "storage_api.h"
#include "db_model.h"
#include "dht_network_client.h"
#include "device_config_dbo.h"
#include "device_record_dbo.h"
#include "member_user.h"

vds::async_task<std::shared_ptr<vds::json_value>> vds::storage_api::device_storages(
  const vds::service_provider * sp,
  const std::shared_ptr<user_manager> & user_mng,
  const http_request & request)
{
  auto result = std::make_shared<json_array>();

  co_await sp->get<db_model>()->async_read_transaction([sp, user_mng, result](database_read_transaction & t) {
    auto client = sp->get<dht::network::client>();
    auto current_node = client->current_node_id();

    orm::device_config_dbo t1;
    orm::device_record_dbo t2;
    db_value<int64_t> used_size;
    auto st = t.get_reader(
      t1.select(
        t1.name,
        t1.node_id,
        t1.local_path,
        t1.reserved_size,
        db_sum(t2.data_size).as(used_size))
      .left_join(t2, t2.local_path == t1.local_path && t2.node_id == t1.node_id)
      .where(t1.owner_id == user_mng->get_current_user().user_certificate()->subject())
      .group_by(t1.name, t1.node_id, t1.local_path, t1.reserved_size));
    while (st.execute()) {
      auto item = std::make_shared<json_object>();
      item->add_property("name", t1.name.get(st));
      item->add_property("local_path", t1.local_path.get(st));
      item->add_property("reserved_size", t1.reserved_size.get(st));
      item->add_property("used_size", std::to_string(used_size.get(st)));
      if(t1.node_id.get(st) == current_node) {
        item->add_property("free_size", std::to_string(foldername(t1.local_path.get(st)).free_size()));
        item->add_property("current", "true");
      }
      else {
        item->add_property("current", "false");
      }
      result->add(item);
    }
  });

  co_return std::static_pointer_cast<json_value>(result);
}

std::shared_ptr<vds::json_value> vds::storage_api::device_storage_label(
  const std::shared_ptr<user_manager>& user_mng)
{
  auto result = std::make_shared<json_object>();
  
  auto user = user_mng->get_current_user();
  result->add_property("vds", "0.1");
  result->add_property("name", user.user_certificate()->subject());
  result->add_property(
    "sign",
    base64::from_bytes(
      asymmetric_sign::signature(
        hash::sha256(),
        *user.private_key(),
        base64::to_bytes(user.user_certificate()->subject()))));

  return std::static_pointer_cast<json_value>(result);
}

vds::async_task<void> vds::storage_api::add_device_storage(
  const vds::service_provider * sp,
  const std::shared_ptr<user_manager> & user_mng,
  const std::string & name,
  const std::string & local_path,
  uint64_t reserved_size)
{  
  auto json = json_parser::parse("vds_storage.json", file::read_all(filename(foldername(local_path), "vds_storage.json")));
  auto sign_info = std::dynamic_pointer_cast<json_object>(json);
  if(!sign_info) {
    throw std::runtime_error("Invalid format");
  }

  std::string value;
  if(!sign_info->get_property("vds", value) || value != "0.1") {
    throw std::runtime_error("Invalid file version");
  }

  auto user = user_mng->get_current_user();
  if (!sign_info->get_property("name", value) || value != user.user_certificate()->subject()) {
    throw std::runtime_error("Invalid user name");
  }

  if (!sign_info->get_property("sign", value)
    || asymmetric_sign_verify::verify(
      hash::sha256(),
      user.user_certificate()->public_key(),
      base64::to_bytes(value),
      base64::to_bytes(user.user_certificate()->subject()))) {
    throw std::runtime_error("Invalid signature");
  }

  return sp->get<db_model>()->async_transaction([sp, user_mng, user, local_path, name, reserved_size](database_transaction & t) {
    auto client = sp->get<dht::network::client>();
    auto current_node = client->current_node_id();

    auto storage_key = std::make_shared<asymmetric_private_key>(asymmetric_private_key::generate(asymmetric_crypto::rsa4096()));
    asymmetric_public_key public_key(*storage_key);

    certificate::create_options options;
    options.name = "!Storage Cert";
    options.country = "RU";
    options.organization = "IVySoft";
    options.ca_certificate = user.user_certificate().get();
    options.ca_certificate_private_key = user.private_key().get();

    auto storage_cert = std::make_shared<certificate>(certificate::create_new(public_key, *storage_key, options));

    orm::device_config_dbo t1;
    t.execute(
      t1.insert(
        t1.node_id = client->current_node_id(),
        t1.local_path = local_path,
        t1.owner_id = user.user_certificate()->subject(),
        t1.name = name,
        t1.reserved_size = reserved_size,
        t1.cert = storage_cert->der(),
        t1.private_key = storage_key->der(std::string())));
  });
}
