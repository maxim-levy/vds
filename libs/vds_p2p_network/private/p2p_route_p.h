#ifndef __VDS_P2P_NETWORK_P2P_ROUTE_P_H_
#define __VDS_P2P_NETWORK_P2P_ROUTE_P_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/
#include "udp_transport.h"
#include "p2p_node_info.h"

namespace vds {

  class _p2p_route {
  public:

    async_task<> send_to(
        const service_provider & sp,
        const guid & node_id,
        const const_data_buffer & message);

    bool random_broadcast(
        const service_provider &sp,
        const const_data_buffer &message);

    void add(const service_provider &sp, const guid &partner_id,
                 const std::shared_ptr<udp_transport::_session> &session);

    std::set<p2p::p2p_node_info> get_neighbors() const;

    void broadcast(
        const service_provider & sp,
        const const_data_buffer & message) const;

    bool send(
        const service_provider &sp,
        const guid &device_id,
        const const_data_buffer &message);

    void close_session(
        const service_provider &sp,
        const guid &partner,
        const std::shared_ptr<std::exception> & ex);

	void save_data(
		const service_provider& sp,
		const guid& this_device_id,
		const guid& user_id,
		const const_data_buffer& data);

  private:
    class session {
    public:
      session(const std::shared_ptr<udp_transport::_session> & target)
      : target_(target), is_busy_(false){
      }

      void lock();
      void unlock();

      bool is_busy() const {
        return this->is_busy_;
      }

      async_task<> route(const service_provider & sp,
        const guid &node_id,
        const const_data_buffer &message);

      void send(const service_provider &sp,
                const const_data_buffer &message);

      void close(
          const service_provider &sp,
          const std::shared_ptr<std::exception> & ex);
    private:
      std::shared_ptr<udp_transport::_session> target_;

      std::mutex state_mutex_;
      bool is_busy_;
    };

    std::map<guid, std::shared_ptr<session>> sessions_;
    mutable std::shared_mutex sessions_mutex_;

    static size_t calc_distance(const const_data_buffer & source_node, const const_data_buffer & target_node);
  };

}

#endif //__VDS_P2P_NETWORK_P2P_ROUTE_P_H_
