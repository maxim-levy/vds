/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "stdafx.h"
#include "background_app.h"
#include "http_router.h"
#include "user_manager.h"

vds::vds_cmd_app::vds_cmd_app()
: server_start_command_set_("Server start", "Start server", "start", "server"),
  server_root_cmd_set_("Install Root node", "Create new network", "root", "server"),
  server_init_command_set_("Initialize new node", "Attach this device to the network", "init", "server"),
  user_login_(
      "l",
      "login",
      "Login",
      "User login"),
  user_password_(
      "p",
      "password",
      "Password",
      "User password"),
  port_(
    "P",
    "port",
    "Port",
    "Port to listen connections")
{
}

void vds::vds_cmd_app::main(const service_provider * sp)
{
  if (this->current_command_set_ == &this->server_start_command_set_
    || &this->server_root_cmd_set_ == this->current_command_set_) {

    this->server_
      .start_network((uint16_t)(this->port_.value().empty() ? 8050 : strtol(this->port_.value().c_str(), nullptr, 10)))
      .get();

    if (&this->server_root_cmd_set_ == this->current_command_set_) {
      const auto data = file::read_all(filename("keys"));
      const_data_buffer root_private_key, common_news_write_private_key, common_news_admin_private_key;
      binary_deserializer s(data);
      s
        >> root_private_key
        >> common_news_write_private_key
        >> common_news_admin_private_key
        ;

      cert_control::private_info_t private_info;
      private_info.root_private_key_ = std::make_shared<asymmetric_private_key>(
        asymmetric_private_key::parse_der(root_private_key, this->user_password_.value()));

      private_info.common_news_write_private_key_ = std::make_shared<asymmetric_private_key>(
        asymmetric_private_key::parse_der(common_news_write_private_key, this->user_password_.value()));

      private_info.common_news_admin_private_key_ = std::make_shared<asymmetric_private_key>(
        asymmetric_private_key::parse_der(common_news_admin_private_key, this->user_password_.value()));


      auto user_mng = std::make_shared<user_manager>(sp);
      user_mng->reset(
        this->user_login_.value(),
        this->user_password_.value(),
        private_info);
    }
    else {
      for (;;) {
        std::cout << "Enter command:\n";

        std::string cmd;
        std::cin >> cmd;

        if ("exit" == cmd) {
          break;
        }
      }

    }
  }
}

void vds::vds_cmd_app::register_services(vds::service_registrator& registrator)
{
  base_class::register_services(registrator);
  registrator.add(this->mt_service_);
  registrator.add(this->task_manager_);
  registrator.add(this->network_service_);
  registrator.add(this->crypto_service_);
  
  if (&this->server_start_command_set_ == this->current_command_set_
      || &this->server_root_cmd_set_ == this->current_command_set_
      || &this->server_init_command_set_ == this->current_command_set_){
    registrator.add(this->server_);
  }
}

void vds::vds_cmd_app::register_command_line(command_line & cmd_line)
{
  base_class::register_command_line(cmd_line);

  cmd_line.add_command_set(this->server_start_command_set_);
  this->server_start_command_set_.optional(this->port_);

  cmd_line.add_command_set(this->server_root_cmd_set_);
  this->server_root_cmd_set_.required(this->user_login_);
  this->server_root_cmd_set_.required(this->user_password_);
  this->server_root_cmd_set_.optional(this->port_);

  //cmd_line.add_command_set(this->server_init_command_set_);
  //this->server_init_command_set_.required(this->user_login_);
  //this->server_init_command_set_.required(this->user_password_);
  //this->server_init_command_set_.optional(this->node_name_);
  //this->server_init_command_set_.optional(this->port_);
}

void vds::vds_cmd_app::start_services(service_registrator & registrator, service_provider * sp)
{
  if (&this->server_root_cmd_set_ == this->current_command_set_) {
    auto folder = persistence::current_user(sp);
    folder.delete_folder(true);
    folder.create();
    registrator.start();
  //} else if (&this->server_init_command_set_ == this->current_command_set_) {
  //  foldername folder(persistence::current_user(sp), ".vds");
  //  folder.delete_folder(true);
  //  folder.create();
  //  registrator.start(sp);
  }
  else {
    base_class::start_services(registrator, sp);
  }
}

bool vds::vds_cmd_app::need_demonize()
{
  return false;
  //return (this->current_command_set_ == &this->server_start_command_set_);
}
