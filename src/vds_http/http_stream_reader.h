#ifndef __VDS_HTTP_HTTP_STREAM_READER_H_
#define __VDS_HTTP_HTTP_STREAM_READER_H_

namespace vds {
  template <
    typename next_method_type,
    typename error_method_type
  >
  class http_stream_reader
  {
  public:
    http_stream_reader()
    {

    }

    bool read_async()
    {
      return false;
    }
    
    void reset(
      next_method_type & next_method,
      error_method_type & error_method
    )
    {
      this->next_method_ = &next_method;
      this->error_method_ = &error_method;
    }

  private:
    next_method_type * next_method_;
    error_method_type * error_method_;
  };
}

#endif//__VDS_HTTP_HTTP_STREAM_READER_H_
