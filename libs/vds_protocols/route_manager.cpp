/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/
#include "stdafx.h"
#include "route_manager.h"
#include "route_manager_p.h"
#include "connection_manager_p.h"

vds::route_manager::route_manager()
  : impl_(new _route_manager())
{
}

vds::route_manager::~route_manager()
{
  delete this->impl_;
}

void vds::route_manager::add_route(
  const service_provider& sp,
  const guid& source_server_id,
  const guid& target_server_id,
  const std::string& address)
{

}

void vds::route_manager::get_routes(
  const service_provider& sp,
  const guid& target_server_id,
  const std::function<bool(size_t metric, std::list<network_route> & routes)> & callback)
{

}

void vds::route_manager::send_to(
  const service_provider & sp,
  const guid & server_id,
  uint32_t message_type_id,
  const const_data_buffer & message_data)
{
  bool result = false;
  auto con_man = sp.get<iconnection_manager>();
  (*con_man)->enum_sessions(
    [&result, sp, con_man, server_id, message_type_id, message_data](connection_session & session)->bool{
      if(server_id == session.server_id()){
       (*con_man)->send_to(sp, session, message_type_id, message_data);
       result = true;
       return false;
      }
      
      return true;
    });
  
  if(result){
    return;
  }
  
  con_man->broadcast(sp, route_message(server_id, message_type_id, message_data));
}
//////////////////////////////////
vds::_route_manager::_route_manager()
{
}

vds::_route_manager::~_route_manager()
{
}

void vds::_route_manager::on_session_started(
  const service_provider& sp,
  const guid & source_server_id,
  const guid & target_server_id,
  const std::string & address)
{
}

