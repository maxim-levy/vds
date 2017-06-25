#ifndef __VDS_PROTOCOLS_CHUNK_MANAGER_P_H_
#define __VDS_PROTOCOLS_CHUNK_MANAGER_P_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "chunk_manager.h"
#include "server_database.h"
#include "chunk_storage.h"

namespace vds {
  class istorage_log;

  class _chunk_manager : public ichunk_manager
  {
  public:
    _chunk_manager();
    ~_chunk_manager();

    void start(const service_provider & sp);
    void stop(const service_provider & sp);

    async_task<> add_object(
      const service_provider & sp,
      const guid & version_id,
      const filename & tmp_file,
      const const_data_buffer & file_hash);

    /*
    
    async_task<>
    add(
        const service_provider & sp,
        const guid & owner_principal,
        server_log_file_map & target,
        const filename & fn);

    void set_next_index(
      const service_provider & sp,
      uint64_t next_index);

      */
  private:
    chunk_storage chunk_storage_;

    std::mutex chunk_mutex_;
    uint64_t last_chunk_;
    
    std::mutex tail_chunk_mutex_;
    uint64_t tail_chunk_index_;
    size_t tail_chunk_size_;
    
    static const size_t BLOCK_SIZE = 16 * 1024 * 1024;
    static const uint16_t MIN_HORCRUX = 512;
    static const uint16_t GENERATE_HORCRUX = 1024;
    static const uint16_t REPLICA_SIZE = BLOCK_SIZE / MIN_HORCRUX;

    
    //
    //std::mutex tmp_folder_mutex_;
    //uint64_t last_tmp_file_index_;
    //
    //std::mutex obj_folder_mutex_;
    //uint64_t last_obj_file_index_;
    //uint64_t obj_size_;

    //static constexpr uint64_t  max_obj_size_ = 1024 * 1024 * 1024;

    //enum chunk_block_type
    //{
    //  cbt_start_stream = 1,
    //  cbt_add_stream = 2,
    //  cbt_finish_stream = 3
    //};

    //static constexpr size_t output_file_max_size = 1024;

    //void generate_chunk(const service_provider & sp);
    
    bool write_chunk(
      const service_provider & sp,
      principal_log_new_object_map & result_record,
      const filename & tmp_file,
      size_t offset,
      size_t size,
      const error_handler & on_error);

    bool write_tail(
      const service_provider & sp,
      principal_log_new_object_map & result_record,
      const filename & tmp_file,
      size_t offset,
      size_t size,
      const error_handler & on_error);

    bool generate_horcruxes(
      const service_provider & sp,
      const guid & server_id,
      chunk_info & chunk_info,
      const std::vector<uint8_t> & buffer,
      const error_handler & on_error);

    bool generate_tail_horcruxes(
      const service_provider & sp,
      const guid & server_id,
      size_t chunk_index,
      const error_handler & on_error);

  };

  class  download_object_broadcast
  {
  public:
    static const char message_type[];
    static const uint32_t message_type_id;
    download_object_broadcast(
      const const_data_buffer & data);
    void serialize(binary_serializer & b) const;
    std::shared_ptr<json_value> serialize() const;

    download_object_broadcast(
      const guid & request_id,
      const guid & server_id,
      uint64_t index);

    const guid & request_id() const { return this->request_id_; }
    const guid & target_server() const { return this->target_server_; }
    const guid & server_id() const { return this->server_id_; }
    uint64_t index() const { return this->index_; }
  private:
    guid request_id_;
    guid target_server_;
    guid server_id_;
    uint64_t index_;
  };
}

#endif // __VDS_PROTOCOLS_CHUNK_MANAGER_P_H_
