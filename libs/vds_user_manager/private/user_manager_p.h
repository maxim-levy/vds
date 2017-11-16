#ifndef __VDS_USER_MANAGER_USER_MANAGER_P_H_
#define __VDS_USER_MANAGER_USER_MANAGER_P_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "user_manager.h"

namespace vds {
  class _user_manager
  {
  public:
    _user_manager();
    ~_user_manager();

    member_user create_root_user(
      class database_transaction & t,
      const std::string & user_name,
      const std::string & user_password,
      const asymmetric_private_key & private_key);

    user_channel create_channel(
        const member_user &owner,
        const vds::asymmetric_private_key & owner_user_private_key,
        const std::string &name);

  };
}

#endif // __VDS_USER_MANAGER_USER_MANAGER_P_H_
