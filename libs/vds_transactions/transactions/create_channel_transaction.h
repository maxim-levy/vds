#ifndef __VDS_USER_MANAGER_CREATE_CHANNEL_TRANSACTION_H_
#define __VDS_USER_MANAGER_CREATE_CHANNEL_TRANSACTION_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include <stdafx.h>
#include "guid.h"
#include "transaction_log.h"
#include "transaction_id.h"

namespace vds {
  namespace transactions {
    class create_channel_transaction {
    public:
      static const uint8_t message_id = (uint8_t)transaction_id::create_channel_transaction;

      create_channel_transaction(
          const guid &id,
          const guid &owner_id);

      create_channel_transaction(binary_deserializer &s) {
        s >> this->id_ >> this->owner_id_;
      }

      const guid &id() const { return id_; }

      const guid &owner_id() const { return owner_id_; }

      binary_serializer &serialize(binary_serializer &s) {
        s << this->id_ << this->owner_id_;
        return s;
      }

    private:
      guid id_;
      guid owner_id_;

    };
  }
}

#endif //__VDS_USER_MANAGER_CREATE_CHANNEL_TRANSACTION_H_
