#ifndef __VDS_SERVER_SERVER_HTTP_API_P_H_
#define __VDS_SERVER_SERVER_HTTP_API_P_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "server_json_api.h"
#include "server_json_client_api.h"
#include "http_router.h"
#include "tcp_network_socket.h"
#include "tcp_socket_server.h"
#include "http_middleware.h"

namespace vds {
  class _server_http_api
    : public server_http_api,
      private http_router
  {
  public:
    _server_http_api();

    async_task<> start(
      const service_provider & sp,
      const std::string & address,
      int port,
      certificate & certificate,
      asymmetric_private_key & private_key);

    void stop(const service_provider & sp);

    //override http_router
    std::shared_ptr<http_message> route(
      const service_provider & sp,
      const std::shared_ptr<http_message> & request) const;

  private:
    tcp_socket_server server_;
    http_middleware<_server_http_api> middleware_;
    server_json_client_api server_json_client_api_;

    class server_http_handler
    {
    public:
      server_http_handler(
        const http_router & router
      );
      
      template<
        typename prev_handler_type,
        typename next_handler_type,
        typename error_handler_type
      >
      void route(
        const service_provider & sp,
        const http_request & request,
        http_incoming_stream & incoming_stream,
        http_response & response,
        http_outgoing_stream & outgoing_stream,
        prev_handler_type & prev_handler,
        next_handler_type & next_handler,
        error_handler_type & error_handler
      ) const
      {
        if("/vds/client_api" == request.url()){
          dataflow(
            http_stream_reader<prev_handler_type>(prev_handler, incoming_stream),
            json_parser("client_api"),
            http_json_api<server_json_client_api>(sp, this->server_json_client_api_),
            http_json_formatter(response, outgoing_stream)
          )(
            next_handler,
            error_handler,
            sp
          );
        }
        else {
          this->router_.route<prev_handler_type, next_handler_type, error_handler_type>(
            sp,
            request,
            incoming_stream,
            response,
            outgoing_stream,
            prev_handler,
            next_handler,
            error_handler
          );
        }
      }
    private:
      server_json_api server_json_api_;
      server_json_client_api server_json_client_api_;
      const http_router & router_;
    };


    class socket_session
    {
    public:
      socket_session(
        const http_router & router,
        const certificate & certificate,
        const asymmetric_private_key & private_key
      );

      class handler
      {
      public:
        handler(
          const socket_session & owner,
          tcp_network_socket & s);

        void start(const service_provider & sp);

      private:
        tcp_network_socket s_;
        ssl_tunnel tunnel_;
        const certificate & certificate_;
        const asymmetric_private_key & private_key_;
        server_http_handler server_http_handler_;

        std::function<void(const service_provider & sp)> http_server_done_;
        std::function<void(const service_provider & sp, std::exception_ptr)> http_server_error_;
      };
    private:
      const http_router & router_;
      const certificate & certificate_;
      const asymmetric_private_key & private_key_;

    };
  };
}

#endif // __VDS_SERVER_SERVER_HTTP_API_P_H_
