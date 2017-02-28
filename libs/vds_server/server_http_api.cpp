/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "stdafx.h"
#include "server.h"
#include "server_http_api.h"
#include "server_http_api_p.h"

vds::server_http_api::server_http_api(const service_provider& sp)
: sp_(sp)
{
}


void vds::server_http_api::start(const std::string & address, int port)
{
  this->impl_.reset(new _server_http_api(this->sp_));
  this->impl_->start(address, port);
}

/////////////////////////////
static void collect_wwwroot(
  vds::http_router & router,
  const vds::foldername & folder,
  const std::string & root_folder
)
{
  if (!folder.exist()) {
    return;
  }

  folder.files(
    [&router, &root_folder](const vds::filename & fn) -> bool {
    router.add_file(
      root_folder + fn.name(),
      fn
    );

    return true;
  });

  folder.folders(
    [&router, &root_folder](const vds::foldername & fn) -> bool {
    collect_wwwroot(router, fn, root_folder + fn.name() + "/");
    return true;
  });
}

vds::_server_http_api::_server_http_api(const service_provider& sp)
: sp_(sp)
{
}

void vds::_server_http_api::start(const std::string & address, int port)
{
  this->router_.reset(new http_router(this->sp_));

  collect_wwwroot(
    *this->router_,
    foldername(foldername(persistence::current_user(this->sp_), ".vds"), "wwwroot"),
    "/");

  this->router_->add_file(
    "/",
    filename(foldername(foldername(persistence::current_user(this->sp_), ".vds"), "wwwroot"), "index.html"));

  //upnp_client upnp(sp);
  //upnp.open_port(8000, 8000, "TCP", "VDS Service");

  this->certificate_.load(filename(foldername(persistence::current_user(this->sp_), ".vds"), "server.crt"));
  this->private_key_.load(filename(foldername(persistence::current_user(this->sp_), ".vds"), "server.pkey"));

  sequence(
    socket_server(this->sp_, address.c_str(), port),
    vds::for_each<const service_provider &, network_socket>::create_handler(
      socket_session(*this->router_, this->certificate_, this->private_key_))
  )
  (
    []() {
      std::cout << "HTTP server closed\n";
    },
    [] (std::exception * ex) {
      std::cout << "Server error: " << ex->what() << "\n";
      delete ex;
    }
  );
}

vds::_server_http_api::socket_session::socket_session(
  const http_router & router,
  const certificate & certificate,
  const asymmetric_private_key & private_key)
  : router_(router), certificate_(certificate),
  private_key_(private_key)
{
}

vds::_server_http_api::socket_session::handler::handler(
  const socket_session & owner,
  const service_provider & sp,
  vds::network_socket & s)
  : sp_(sp),
  s_(std::move(s)),
  tunnel_(sp, false, &owner.certificate_, &owner.private_key_),
  certificate_(owner.certificate_),
  private_key_(owner.private_key_),
  server_logic_(sp, owner.router_),
  done_handler_(this),
  error_handler_([this](std::exception *) {delete this; }),
  http_server_done_([this]() {}),
  http_server_error_([this](std::exception *) {})
{
}

void vds::_server_http_api::socket_session::handler::start()
{
  vds::sequence(
    input_network_stream(this->sp_, this->s_),
    ssl_input_stream(this->tunnel_),
    http_parser(this->sp_),
    http_middleware<server_logic>(this->server_logic_),
    http_response_serializer(),
    ssl_output_stream(this->tunnel_),
    output_network_stream(this->sp_, this->s_)
  )
  (
    this->done_handler_,
    this->error_handler_
    );
}
