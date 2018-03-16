#ifndef __VDS_HTTP_HTTP_ROUTER_H_
#define __VDS_HTTP_HTTP_ROUTER_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/
#include <map>

namespace vds {
  class http_message;
  class filename;
  
  class http_router
  {
  public:
    http_router();

    http_message route(
      const service_provider & sp,
      const http_message & request,
      const std::string & local_path) const;
    
    void add_static(
      const std::string & url,
      const std::string & response);

    void add_file(
      const std::string & url,
      const filename & filename);
    
    
  private:
    std::map<std::string, std::string> static_;
    std::map<std::string, filename> files_;
  };
}

#endif // __VDS_HTTP_HTTP_ROUTER_H_
