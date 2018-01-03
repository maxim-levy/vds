#ifndef __VDS_FILE_MANAGER_FILE_OPERATIONS_H_
#define __VDS_FILE_MANAGER_FILE_OPERATIONS_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/
#include <string>
#include "const_data_buffer.h"
#include "async_task.h"
#include "filename.h"

namespace vds {
  namespace file_manager_private {
    class _file_operations;
  };

  namespace file_manager {
    class file_operations {
    public:
      static const size_t BLOCK_SIZE = 16 * 1024 * 1024;
      static const uint16_t MIN_HORCRUX = 512;
      static const uint16_t GENERATE_HORCRUX = 1024;

      file_operations();

      async_task<> upload_file(
          const service_provider & sp,
          const std::string & name,
          const std::string & mimetype,
          const filename & file_path);

    protected:
      std::shared_ptr<file_manager_private::_file_operations> impl_;
    };
  }
}

#endif //__VDS_FILE_MANAGER_FILE_OPERATIONS_H_
