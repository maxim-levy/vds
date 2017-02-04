/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "stdafx.h"
#include "node_app.h"

vds::node_app::node_app()
:
add_storage_cmd_set_(
  "Add new storage",
  "Add new storage to the network",
  "add",
  "storage"
),
remove_storage_cmd_set_(
  "Remove storage",
  "Remove storage to the network",
  "remove",
  "storage"
),
list_storage_cmd_set_(
  "List storage",
  "List storage to the network",
  "list",
  "storage"
),
storage_path_(
  "s", "storage",
  "Storage", "Path to the storage"
)
{

}

void vds::node_app::main(
  const vds::service_provider& sp)
{

}

void vds::node_app::register_command_line(vds::command_line& cmd_line)
{
  cmd_line.add_command_set(this->add_storage_cmd_set_);
  this->add_storage_cmd_set_.required(this->storage_path_);

}
