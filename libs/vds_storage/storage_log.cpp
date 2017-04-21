/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "stdafx.h"
#include "storage_log.h"
#include "storage_log_p.h"
#include "process_log_line.h"
#include "log_records.h"
#include "cert.h"
#include "node.h"
#include "endpoint.h"
#include "certificate_authority.h"
#include "certificate_authority_p.h"
#include "chunk_storage.h"

vds::storage_log::storage_log(
  const service_provider & sp,
  const guid & current_server_id,
  const certificate & server_certificate,
  const asymmetric_private_key & server_private_key)
  : impl_(new _storage_log(sp,
    current_server_id,
    server_certificate,
    server_private_key,
    this))
{
}

vds::storage_log::~storage_log()
{
}

void vds::storage_log::start()
{
  this->impl_->start();
}

void vds::storage_log::stop()
{
  this->impl_->stop();
}

const vds::guid & vds::istorage_log::current_server_id() const
{
  return this->owner_->impl_->current_server_id();
}

const vds::certificate & vds::istorage_log::server_certificate() const
{
  return this->owner_->impl_->server_certificate();
}

const vds::asymmetric_private_key & vds::istorage_log::server_private_key() const
{
  return this->owner_->impl_->server_private_key();
}

bool vds::istorage_log::is_empty() const
{
  return this->owner_->impl_->is_empty();
}

size_t vds::istorage_log::minimal_consensus() const
{
  return this->owner_->impl_->minimal_consensus();
}

void vds::istorage_log::add_record(const std::string & record)
{
  return this->owner_->impl_->add_record(record);
}

void vds::istorage_log::add_to_local_log(const json_value * record)
{
  this->owner_->impl_->add_to_local_log(record);
}

size_t vds::istorage_log::new_message_id()
{
  return this->owner_->impl_->new_message_id();
}

vds::async_task<> vds::istorage_log::register_server(const std::string & server_certificate)
{
  return this->owner_->impl_->register_server(server_certificate);
}

std::unique_ptr<vds::cert> vds::istorage_log::find_cert(const std::string & object_name) const
{
  return this->owner_->impl_->find_cert(object_name);
}

std::unique_ptr<vds::const_data_buffer> vds::istorage_log::get_object(const vds::full_storage_object_id& object_id)
{
  return this->owner_->impl_->get_object(object_id);
}

vds::event_source<const vds::server_log_record&, const vds::const_data_buffer&>& vds::istorage_log::new_local_record_event()
{
  return this->owner_->impl_->new_local_record_event();
}

void vds::istorage_log::add_endpoint(
  const std::string & endpoint_id,
  const std::string & addresses)
{
  this->owner_->impl_->add_endpoint(endpoint_id, addresses);
}

void vds::istorage_log::get_endpoints(std::map<std::string, std::string> & addresses)
{
  this->owner_->impl_->get_endpoints(addresses);
}

vds::async_task<> vds::istorage_log::save_file(
  const std::string & version_id,
  const std::string & user_login,
  const std::string & name,
  const filename & tmp_file)
{
  return this->owner_->impl_->save_file(version_id, user_login, name, tmp_file);
}

vds::async_task<> vds::istorage_log::reset(
  const vds::certificate & root_certificate,
  const asymmetric_private_key & private_key,
  const std::string & root_password,
  const std::string & addresses)
{
  return this->owner_->impl_->reset(root_certificate, private_key, root_password, addresses);
}

void vds::istorage_log::apply_record(
  const server_log_record & record,
  const const_data_buffer & signature,
  bool check_signature /*= true*/)
{
  this->owner_->impl_->apply_record(record, signature, check_signature);
}

///////////////////////////////////////////////////////////////////////////////
vds::_storage_log::_storage_log(
  const service_provider & sp,
  const guid & current_server_id,
  const certificate & server_certificate,
  const asymmetric_private_key & server_private_key,
  storage_log * owner)
: sp_(sp),
  server_certificate_(server_certificate),
  current_server_key_(server_private_key),
  current_server_id_(current_server_id),
  owner_(owner),
  log_(sp, "Server log"),
  vds_folder_(persistence::current_user(sp), ".vds"),
  local_log_index_(0),
  is_empty_(true),
  minimal_consensus_(0),
  last_message_id_(0)
{
}

vds::async_task<> vds::_storage_log::reset(
  const certificate & root_certificate,
  const asymmetric_private_key & private_key,
  const std::string & password,
  const std::string & addresses
)
{
  this->vds_folder_.create();
  
  hash ph(hash::sha256());
  ph.update(password.c_str(), password.length());
  ph.final();


  return this->save_object(
    object_container()
      .add("c", root_certificate.str())
      .add("k", private_key.str(password)))
    .then([this, ph_signature = ph.signature(), addresses](
        const std::function<void(void)> & done,
        const error_handler & on_error,
        const vds::storage_object_id & user_cert_id) {
 
      this->add_to_local_log(
        server_log_root_certificate(
          user_cert_id,
          ph_signature).serialize().get());

      this->add_to_local_log(server_log_new_server(this->server_certificate_.str()).serialize().get());
      this->add_to_local_log(server_log_new_endpoint(this->current_server_id_, addresses).serialize().get());
        
      this->db_.get(this->sp_).add_cert(
        cert(
          "login:root",
          full_storage_object_id(this->current_server_id_, user_cert_id),
          ph_signature));
        
      done();
    });
}


void vds::_storage_log::start()
{
  this->local_log_index_ = this->db_.get(this->sp_).get_server_log_max_index(this->current_server_id_) + 1;
  this->sp_.get<itask_manager>().wait_for(std::chrono::seconds(5), [this](){
    this->process_timer_jobs();
  });
}

void vds::_storage_log::stop()
{
}


bool vds::_storage_log::is_empty()
{
  return this->is_empty_;
}

vds::certificate * vds::_storage_log::get_cert(const std::string & subject)
{
  return nullptr;
}

vds::certificate * vds::_storage_log::parse_root_cert(const json_value * value)
{
  server_log_root_certificate message(value);
  
  //std::string cert_body;
  //if (message.certificate().empty() || message.private_key().empty()) {
  //  return nullptr;
  //}

  //return new certificate(certificate::parse(message.certificate()));
  return nullptr;
}

void vds::_storage_log::apply_record(const guid & source_server_id, const json_value * value)
{
  if(this->is_empty_){
    //already processed
    this->is_empty_ = false;
  }

  auto value_obj = dynamic_cast<const json_object *>(value);
  if (nullptr != value_obj) {
    std::string record_type;
    if (value_obj->get_property("$t", record_type, false) && !record_type.empty()) {
      if (server_log_root_certificate::message_type == record_type) {
        this->process(source_server_id, server_log_root_certificate(value_obj));
      }
      else if (server_log_new_server::message_type == record_type) {
        this->process(source_server_id, server_log_new_server(value_obj));
      }
      else if (server_log_new_endpoint::message_type == record_type) {
        this->process(source_server_id, server_log_new_endpoint(value_obj));
      }
      else {
        this->log_(log_level::ll_warning, "Invalid server log record type %s", record_type.c_str());
      }
    }
    else {
      this->log_(log_level::ll_warning, "Invalid server log record: the record has not type attribute");
    }
  }
  else {
    this->log_(log_level::ll_warning, "Invalid server log record: the record in not object");
  }
}

void vds::_storage_log::process(const guid & source_server_id, const server_log_root_certificate & message)
{
  //auto cert = new certificate(certificate::parse(message.certificate()));
  //this->certificate_store_.add(*cert);
  //this->loaded_certificates_[cert->subject()].reset(cert);

  this->db_.get(this->sp_).add_cert(
    vds::cert(
      "login:root",
      full_storage_object_id(
        source_server_id,
        message.user_cert()),
      message.password_hash()));
}

void vds::_storage_log::process(const guid & source_server_id, const server_log_new_server & message)
{
  //auto cert = new certificate(certificate::parse(message.certificate()));
  //auto result = this->certificate_store_.verify(*cert);

  //if (result.error_code != 0) {
  //  throw new std::runtime_error("Invalid certificate");
  //}

  //this->certificate_store_.add(*cert);
  //this->loaded_certificates_[cert->subject()].reset(cert);

  //this->nodes_.push_back(node(cert->subject(), message.certificate()));
  //this->log_(ll_trace, "add node %s", cert->subject().c_str());
}

void vds::_storage_log::process(const guid & source_server_id, const server_log_new_endpoint & message)
{
}

void vds::_storage_log::add_record(const std::string & record)
{
  //dataflow(
  //  json_parser("Record"),
  //  process_log_line<_storage_log>("Record", this)
  //)(
  //  [this, &record]() {
  //  file f(filename(this->commited_folder_, "checkpoint0.json").local_name(), file::append);
  //  output_text_stream os(f);
  //  os.write(record);
  //  os.write("\n");
  //},
  //  [](std::exception * ex) { throw ex; },
  //  record.c_str(),
  //  record.length()
  //);
}

size_t vds::_storage_log::new_message_id()
{
  return this->last_message_id_++;
}

vds::async_task<> vds::_storage_log::register_server(const std::string & server_certificate)
{
  return create_async_task(
    [this, server_certificate](
      const std::function<void(void)> & done,
      const error_handler & on_error) {
    this->add_to_local_log(server_log_new_server(server_certificate).serialize().get());
    done();
  });
}

vds::async_task<const vds::storage_object_id &>
vds::_storage_log::save_object(const object_container & fc)
{
  binary_serializer s;
  fc.serialize(s);
 
  return this->chunk_manager_
    .get(this->sp_)
    .add(s.data())
    .then([this](
      const std::function<void(const storage_object_id &)> & done,
      const error_handler & on_error, 
      const server_log_new_object & index){
    this->add_to_local_log(index.serialize().get());
    done(vds::storage_object_id(index.index()));
    });
}

void vds::_storage_log::add_to_local_log(const json_value * record)
{
  std::lock_guard<std::mutex> lock(this->local_log_mutex_);

  const_data_buffer signature;
  auto result = this->db_
    .get(this->sp_)
    .add_local_record(
      server_log_record::record_id{
        this->current_server_id_,
        this->local_log_index_++ },
      record,
      signature);

  this->new_local_record_event_(result, signature);
}

void vds::_storage_log::apply_record(const server_log_record & record, const const_data_buffer & signature, bool check_signature /*= true*/)
{
  this->log_.debug("Apply record %s:%d", record.id().source_id.str().c_str(), record.id().index);
 
  const json_object * obj = dynamic_cast<const json_object *>(record.message());
  if(nullptr == obj){
    this->log_.error("Wrong messsage in the record %s:%d", record.id().source_id.str().c_str(), record.id().index);
    this->db_.get(this->sp_).delete_record(record.id());
    return;
  }
  
  std::string message_type;
  if (!obj->get_property("$t", message_type)) {
    this->log_.error("Missing messsage type in the record %s:%d", record.id().source_id.str().c_str(), record.id().index);
    this->db_.get(this->sp_).delete_record(record.id());
    return;
  }
  
  if (server_log_new_object::message_type == message_type) {
    server_log_new_object msg(obj);

    this->db_.get(this->sp_).add_object(record.id().source_id, msg);
  }
  else {
    this->log_.error("Enexpected messsage type %s in the record %s:%d",
      message_type.c_str(),
      record.id().source_id.str().c_str(),
      record.id().index);

    this->db_.get(this->sp_).delete_record(record.id());
    return;
  }
  
  
  this->db_.get(this->sp_).processed_record(record.id());
}

std::unique_ptr<vds::cert> vds::_storage_log::find_cert(const std::string & object_name)
{
  return this->db_.get(this->sp_).find_cert(object_name);
}

std::unique_ptr<vds::const_data_buffer> vds::_storage_log::get_object(const vds::full_storage_object_id& object_id)
{
  return this->local_cache_.get(this->sp_).get_object(object_id);
}

void vds::_storage_log::add_endpoint(
  const std::string & endpoint_id,
  const std::string & addresses)
{
  this->db_.get(this->sp_).add_endpoint(endpoint_id, addresses);
}

void vds::_storage_log::get_endpoints(std::map<std::string, std::string> & addresses)
{
  this->db_.get(this->sp_).get_endpoints(addresses);
}

vds::async_task<> vds::_storage_log::save_file(
  const std::string & version_id,
  const std::string & user_login,
  const std::string & name,
  const filename & tmp_file)
{
  return this->chunk_manager_
    .get(this->sp_)
    .add(version_id, user_login, name, tmp_file)
    .then([this](
      const std::function<void(void)> & done,
      const error_handler & on_error,
      const server_log_file_map & fm) {
        this->db_.get(this->sp_).add_file(this->current_server_id_, fm);
        this->add_to_local_log(fm.serialize().get());
        done();
  });

  /*
  auto signature = asymmetric_sign::signature(hash::sha256(), this->current_server_key_, s.data());
  this->db_.add_object(this->current_server_id_, index, signature);
  return vds::storage_object_id(index, signature);
  */
}

void vds::_storage_log::process_timer_jobs()
{
  server_log_record record;
  const_data_buffer signature;
  while(this->db_.get(this->sp_).get_front_record(record, signature)){
    this->apply_record(record, signature);
  }
  
  this->sp_.get<itask_manager>().wait_for(std::chrono::seconds(5), [this](){
    this->process_timer_jobs();
  });
}

