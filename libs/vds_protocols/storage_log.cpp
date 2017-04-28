/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "stdafx.h"
#include "storage_log.h"
#include "storage_log_p.h"
#include "process_log_line.h"
#include "log_records.h"
#include "cert_record.h"
#include "node.h"
#include "endpoint.h"
#include "certificate_authority.h"
#include "certificate_authority_p.h"
#include "chunk_storage.h"
#include "server_log_sync.h"
#include "server_log_sync_p.h"

vds::storage_log::storage_log(
  const guid & current_server_id,
  const certificate & server_certificate,
  const asymmetric_private_key & server_private_key)
  : impl_(new _storage_log(
    current_server_id,
    server_certificate,
    server_private_key,
    this))
{
}

vds::storage_log::~storage_log()
{
}

void vds::storage_log::start(const service_provider & sp)
{
  this->impl_->start(sp);
}

void vds::storage_log::stop(const service_provider & sp)
{
  this->impl_->stop(sp);
}

const vds::guid & vds::istorage_log::current_server_id() const
{
  return static_cast<const _storage_log *>(this)->current_server_id();
}

const vds::certificate & vds::istorage_log::server_certificate() const
{
  return static_cast<const _storage_log *>(this)->server_certificate();
}

const vds::asymmetric_private_key & vds::istorage_log::server_private_key() const
{
  return static_cast<const _storage_log *>(this)->server_private_key();
}

size_t vds::istorage_log::minimal_consensus() const
{
  return static_cast<const _storage_log *>(this)->minimal_consensus();
}

void vds::istorage_log::add_to_local_log(
  const service_provider & sp,
  const json_value * record)
{
  static_cast<_storage_log *>(this)->add_to_local_log(sp, record);
}

size_t vds::istorage_log::new_message_id()
{
  return static_cast<_storage_log *>(this)->new_message_id();
}

vds::async_task<> vds::istorage_log::register_server(
  const service_provider & sp,
  const std::string & server_certificate)
{
  return static_cast<_storage_log *>(this)->register_server(sp, server_certificate);
}

std::unique_ptr<vds::cert_record> vds::istorage_log::find_cert(
  const service_provider & sp,
  const std::string & object_name) const
{
  return static_cast<const _storage_log *>(this)->find_cert(sp, object_name);
}

std::unique_ptr<vds::const_data_buffer> vds::istorage_log::get_object(
  const service_provider & sp,
  const vds::full_storage_object_id& object_id)
{
  return static_cast<_storage_log *>(this)->get_object(sp, object_id);
}

void vds::istorage_log::add_endpoint(
  const service_provider & sp,
  const std::string & endpoint_id,
  const std::string & addresses)
{
  static_cast<_storage_log *>(this)->add_endpoint(sp, endpoint_id, addresses);
}

void vds::istorage_log::get_endpoints(
  const service_provider & sp,
  std::map<std::string, std::string> & addresses)
{
  static_cast<_storage_log *>(this)->get_endpoints(sp, addresses);
}

void vds::istorage_log::reset(
  const service_provider & sp,
  const vds::certificate & root_certificate,
  const asymmetric_private_key & private_key,
  const std::string & root_password,
  const std::string & addresses)
{
  static_cast<_storage_log *>(this)->reset(
    sp,
    root_certificate,
    private_key,
    root_password,
    addresses);
}

void vds::istorage_log::apply_record(
  const service_provider & sp,
  const server_log_record & record,
  const const_data_buffer & signature,
  bool check_signature /*= true*/)
{
  static_cast<_storage_log *>(this)->apply_record(
    sp,
    record,
    signature,
    check_signature);
}

///////////////////////////////////////////////////////////////////////////////
vds::_storage_log::_storage_log(
  const guid & current_server_id,
  const certificate & server_certificate,
  const asymmetric_private_key & server_private_key,
  storage_log * owner)
: server_certificate_(server_certificate),
  current_server_key_(server_private_key),
  current_server_id_(current_server_id),
  owner_(owner),
  local_log_index_(0),
  is_empty_(true),
  minimal_consensus_(0),
  last_message_id_(0)
{
}

void vds::_storage_log::reset(
  const service_provider & sp,
  const certificate & root_certificate,
  const asymmetric_private_key & private_key,
  const std::string & password,
  const std::string & addresses
)
{
  this->vds_folder_ = foldername(persistence::current_user(sp), ".vds");
  this->vds_folder_.create();
  
  hash ph(hash::sha256());
  ph.update(password.c_str(), password.length());
  ph.final();
  
  this->add_to_local_log(
    sp,
    server_log_root_certificate(
      root_certificate.str(),
      private_key.str(password),
      ph.signature()).serialize().get());
  this->add_to_local_log(sp, server_log_new_server(this->server_certificate_.str()).serialize().get());
  this->add_to_local_log(sp, server_log_new_endpoint(this->current_server_id_, addresses).serialize().get());
}


void vds::_storage_log::start(const service_provider & sp)
{
  this->vds_folder_ = foldername(persistence::current_user(sp), ".vds");

  this->local_log_index_ = sp.get<iserver_database>().get_server_log_max_index(this->current_server_id_) + 1;

  this->process_timer_.start(sp, std::chrono::seconds(5), [this, sp](){
    this->process_timer_jobs(sp);
  });
}

void vds::_storage_log::stop(const service_provider & sp)
{
  this->process_timer_.stop(sp);
}


size_t vds::_storage_log::new_message_id()
{
  return this->last_message_id_++;
}

vds::async_task<> vds::_storage_log::register_server(
  const service_provider & sp,
  const std::string & server_certificate)
{
  return create_async_task(
    [this, sp, server_certificate](
      const std::function<void(void)> & done,
      const error_handler & on_error) {
    this->add_to_local_log(sp, server_log_new_server(server_certificate).serialize().get());
    done();
  });
}

vds::async_task<const vds::storage_object_id &>
vds::_storage_log::save_object(
  const service_provider & sp,
  const object_container & fc)
{
  binary_serializer s;
  fc.serialize(s);
 
  return sp.get<ichunk_manager>()
    .add(sp, s.data())
    .then([this](
      const std::function<void(const storage_object_id &)> & done,
      const error_handler & on_error, 
      const server_log_new_object & index){
    done(vds::storage_object_id(index.index()));
    });
}

void vds::_storage_log::add_to_local_log(
  const service_provider & sp,
  const json_value * message)
{
  std::lock_guard<std::mutex> lock(this->record_state_mutex_);

  const_data_buffer signature;
  auto result = sp.get<iserver_database>()
    .add_local_record(
      server_log_record::record_id{
        this->current_server_id_,
        this->local_log_index_++ },
      message,
      signature);
    
  this->apply_record(sp, result, signature);

  sp.get<_server_log_sync>().on_new_local_record(sp, result, signature);
}

void vds::_storage_log::apply_record(
  const service_provider & sp,
  const server_log_record & record,
  const const_data_buffer & signature,
  bool check_signature /*= true*/)
{
  sp.get<logger>().debug(sp, "Apply record %s:%d", record.id().source_id.str().c_str(), record.id().index);
  auto state = sp.get<iserver_database>().get_record_state(record.id());
  if(iserver_database::server_log_state::front != state){
    throw std::runtime_error("Invalid server state");
  }
 
  const json_object * obj = dynamic_cast<const json_object *>(record.message());
  if(nullptr == obj){
    sp.get<logger>().info(sp, "Wrong messsage in the record %s:%d", record.id().source_id.str().c_str(), record.id().index);
    sp.get<iserver_database>().delete_record(record.id());
    return;
  }
  
  try {
    std::string message_type;
    if (!obj->get_property("$t", message_type)) {
      sp.get<logger>().info(sp, "Missing messsage type in the record %s:%d", record.id().source_id.str().c_str(), record.id().index);
      sp.get<iserver_database>().delete_record(record.id());
      return;
    }
    
    if (server_log_new_object::message_type == message_type) {
      server_log_new_object msg(obj);
      
      sp.get<logger>().debug(sp, "Add object %s.%d",
        record.id().source_id.str().c_str(),
        msg.index());

      sp.get<iserver_database>().add_object(record.id().source_id, msg);
    }
    else if(server_log_root_certificate::message_type == message_type){
      server_log_root_certificate msg(obj);
      
      sp.get<iserver_database>().add_cert(
        vds::cert_record(
          "login:root",
          msg.user_cert(),
          msg.user_private_key(),
          msg.password_hash()));
    }
    else if(server_log_new_server::message_type == message_type){
    }
    else if(server_log_new_endpoint::message_type == message_type){
      server_log_new_endpoint msg(obj);
      
      sp.get<iserver_database>().add_endpoint(
        msg.server_id().str(),
        msg.addresses());
    }
    else if(server_log_file_map::message_type == message_type){
      server_log_file_map msg(obj);
      
      sp.get<iserver_database>().add_file(
        record.id().source_id,
        msg);
    }
    else {
      sp.get<logger>().info(sp, "Enexpected messsage type '%s' in the record %s:%d",
        message_type.c_str(),
        record.id().source_id.str().c_str(),
        record.id().index);

      sp.get<iserver_database>().delete_record(record.id());
      return;
    }
  }
  catch(...){
    sp.get<logger>().info(sp, "%s", exception_what(std::current_exception()).c_str());
    sp.get<iserver_database>().delete_record(record.id());
    return;
  }
  
  sp.get<iserver_database>().processed_record(record.id());
}

std::unique_ptr<vds::cert_record> vds::_storage_log::find_cert(
  const service_provider & sp,
  const std::string & object_name) const
{
  return sp.get<iserver_database>().find_cert(object_name);
}

std::unique_ptr<vds::const_data_buffer> vds::_storage_log::get_object(
  const service_provider & sp,
  const vds::full_storage_object_id& object_id)
{
  return sp.get<ilocal_cache>().get_object(object_id);
}

void vds::_storage_log::add_endpoint(
  const service_provider & sp,
  const std::string & endpoint_id,
  const std::string & addresses)
{
  sp.get<iserver_database>().add_endpoint(endpoint_id, addresses);
}

void vds::_storage_log::get_endpoints(
  const service_provider & sp,
  std::map<std::string, std::string> & addresses)
{
  sp.get<iserver_database>().get_endpoints(sp, addresses);
}


  /*
  auto signature = asymmetric_sign::signature(hash::sha256(), this->current_server_key_, s.data());
  this->db_.add_object(this->current_server_id_, index, signature);
  return vds::storage_object_id(index, signature);
  */


void vds::_storage_log::process_timer_jobs(const service_provider & sp)
{
  std::lock_guard<std::mutex> lock(this->record_state_mutex_);

  server_log_record record;
  const_data_buffer signature;
  while(sp.get<iserver_database>().get_front_record(record, signature)){
    this->apply_record(sp, record, signature);
  }
}

