#ifndef __VDS_P2P_NETWORK_P2P_NETWORK_P_H_
#define __VDS_P2P_NETWORK_P2P_NETWORK_P_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/
#include <memory>
#include "udp_transport.h"
#include "url_parser.h"
#include "db_model.h"
#include "well_known_node_dbo.h"
#include "task_manager.h"
#include "shutdown_event.h"
#include "p2p_crypto_tunnel.h"
#include "udp_socket.h"
#include "p2p_route.h"

namespace vds {

  class _p2p_network : public std::enable_shared_from_this<_p2p_network> {
  public:
    _p2p_network(
        const std::shared_ptr<class ip2p_network_client> &client);

    ~_p2p_network();

    async_task<> random_broadcast(
        const vds::service_provider &sp,
        const vds::const_data_buffer &message);

    void add_route(
        const guid &partner_id,
        const guid &this_node_id,
        const std::shared_ptr<udp_transport::_session> & session);

  private:
    udp_server server_;
    std::shared_ptr<class ip2p_network_client> client_;

    p2p_route route_;

  };
}

#endif //__VDS_P2P_NETWORK_P2P_NETWORK_P_H_
