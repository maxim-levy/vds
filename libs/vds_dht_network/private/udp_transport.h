#ifndef __VDS_DHT_NETWORK_UDP_TRANSPORT_H_
#define __VDS_DHT_NETWORK_UDP_TRANSPORT_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "service_provider.h"
#include "udp_socket.h"
#include "legacy.h"
#include "debug_mutex.h"

namespace vds {
  struct session_statistic;
}

namespace vds {
  namespace dht {
    namespace network {
      class udp_transport : public std::enable_shared_from_this<udp_transport> {
      public:
        static constexpr size_t NODE_ID_SIZE = 32;
        static constexpr uint8_t PROTOCOL_VERSION = 0;

        udp_transport();
        udp_transport(const udp_transport&) = delete;
        udp_transport(udp_transport&&) = delete;

        void start(const service_provider& sp, uint16_t port,
                   const const_data_buffer& this_node_id);

        void stop(const service_provider& sp);

        async_task<> write_async(const service_provider& sp, const udp_datagram& datagram);
        async_task<> try_handshake(const service_provider& sp, const std::string& address);

        const const_data_buffer& this_node_id() const {
          return this->this_node_id_;
        }

        void get_session_statistics(session_statistic& session_statistic);

      private:
        const_data_buffer this_node_id_;
        udp_server server_;

        std::list<std::tuple<udp_datagram, async_result<>>> send_queue_;

        std::debug_mutex write_mutex_;
        std::condition_variable write_cond_;
        bool write_in_progress_;
#ifdef _DEBUG
#ifndef _WIN32
        pid_t owner_id_;
#else
        DWORD owner_id_;
#endif//_WIN32
#endif//_DEBUG


        mutable std::shared_mutex sessions_mutex_;
        std::map<network_address, std::shared_ptr<class dht_session>> sessions_;
        timer timer_;

        std::mutex block_list_mutex_;
        std::map<std::string, std::chrono::steady_clock::time_point> block_list_;

        async_task<> on_timer(const service_provider& sp);
        void add_session(const service_provider& sp, const network_address& address,
                         const std::shared_ptr<dht_session>& session);
        std::shared_ptr<dht_session> get_session(const network_address& address) const;

        void continue_read(const service_provider& sp);
        void continue_send(const service_provider& sp);
      };
    }
  }
}


#endif //__VDS_DHT_NETWORK_UDP_TRANSPORT_H_
