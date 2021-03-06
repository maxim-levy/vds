#ifndef __VDS_DHT_NETWORK_DTH_NETWORK_SERVICE_H_
#define __VDS_DHT_NETWORK_DTH_NETWORK_SERVICE_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/
#include "service_provider.h"
#include "const_data_buffer.h"
#include "dht_network_client.h"
#include "asymmetriccrypto.h"

namespace vds {
  class server;

  namespace dht {
    namespace network {
      class udp_transport;

      class service {
      public:
        static constexpr uint16_t MIN_HORCRUX = 8;
        static constexpr uint16_t GENERATE_HORCRUX = 16;

        static constexpr uint16_t MIN_DISTRIBUTED_PIECES = 8;
        static constexpr uint16_t GENERATE_DISTRIBUTED_PIECES = 16;

        static constexpr size_t BLOCK_SIZE = 32 * 1024 * MIN_HORCRUX * MIN_DISTRIBUTED_PIECES;//32K 
        
        void register_services(service_registrator& registrator);
        void start(
          const service_provider * sp,
          const std::shared_ptr<iudp_transport> & udp_transport,
          const uint16_t port);
        void stop();
        vds::async_task<void> prepare_to_stop();

      private:
        client client_;
      };
    }
  }
}

#endif //__VDS_DHT_NETWORK_DTH_NETWORK_SERVICE_H_
