#ifndef __VDS_PROTOCOLS_STORAGE_LOG_P_H_
#define __VDS_PROTOCOLS_STORAGE_LOG_P_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "chunk_manager.h"
#include "server_database.h"
#include "storage_object_id.h"
#include "local_cache.h"
#include "storage_log.h"

namespace vds {
  class server_log_root_certificate;
  class server_log_new_server;
  class server_log_new_endpoint;
  class cert_record;
  class node;
  class endpoint;
  class ichunk_storage;
  
  class _storage_log : public istorage_log
  {
  public:
    _storage_log();
    ~_storage_log();

    void reset(
      const service_provider & sp,
      const certificate & root_certificate,
      const asymmetric_private_key & private_key,
      const std::string & root_password,
      const std::string & addresses);

    void start(
      const service_provider & sp,
      const guid & current_server_id,
      const certificate & server_certificate,
      const asymmetric_private_key & server_private_key);
    void stop(const service_provider & sp);

    size_t minimal_consensus() const { return this->minimal_consensus_; }

    size_t new_message_id();

    vds::async_task<> register_server(
      const service_provider & sp,
      const std::string & server_certificate);

    std::unique_ptr<cert_record> find_cert(
      const service_provider & sp,
      const std::string & object_name) const;

    std::unique_ptr<const_data_buffer> get_object(
      const service_provider & sp,
      const full_storage_object_id & object_id);

    void add_endpoint(
      const service_provider & sp,
      const std::string & endpoint_id,
      const std::string & addresses);

    void get_endpoints(
      const service_provider & sp,
      std::map<std::string, std::string> & addresses);


    const guid & current_server_id() const { return this->current_server_id_; }
    const certificate & server_certificate() const { return this->server_certificate_; }
    const asymmetric_private_key & server_private_key() const { return this->current_server_key_; }
    void add_to_local_log(
      const service_provider & sp,
      const std::shared_ptr<json_value> & record);

    void apply_record(
      const service_provider & sp,
      const server_log_record & record,
      const const_data_buffer & signature,
      bool check_signature = true);

    server_log_record::record_id get_last_applied_record(
      const service_provider & sp);

  private:
    certificate server_certificate_;
    asymmetric_private_key current_server_key_;

    guid current_server_id_;
    foldername vds_folder_;
    server_log_record::record_id last_applied_record_;

    bool is_empty_;
    size_t minimal_consensus_;

    size_t last_message_id_;
    
    std::mutex record_state_mutex_;
    uint64_t local_log_index_;
    
    timer process_timer_;
    bool process_timer_jobs(const service_provider & sp);
  };
}

#endif // __VDS_PROTOCOLS_STORAGE_LOG_P_H_
