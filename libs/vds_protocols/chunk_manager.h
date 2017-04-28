#ifndef __VDS_PROTOCOLS_CHUNK_MANAGER_H_
#define __VDS_PROTOCOLS_CHUNK_MANAGER_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "local_cache.h"
#include "log_records.h"

namespace vds {
  class _chunk_manager;
  
  class chunk_manager
  {
  public:
    chunk_manager();
    ~chunk_manager();

    void start(const service_provider & sp);
    void stop(const service_provider & sp);

  private:
    _chunk_manager * const impl_;
  };

  class ichunk_manager
  {
  public:
    async_task<const server_log_new_object &> add(
      const service_provider & sp,
      const const_data_buffer & data);

    const_data_buffer get(
      const service_provider & sp,
      const guid & server_id,
      uint64_t index);
    
    void set_next_index(
      const service_provider & sp,
      uint64_t next_index);
  };
}

#endif // __VDS_PROTOCOLS_CHUNK_MANAGER_H_
