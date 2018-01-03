#ifndef __VDS_SERVER_SERVER_P_H_
#define __VDS_SERVER_SERVER_P_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "server.h"
#include "p2p_network_service.h"

namespace vds {
  class cert_manager;
  class node_manager;
  class user_manager;
  class server_http_api;
  class server_connection;
  class server_udp_api;

  class _node_manager;
  class _chunk_manager;
  class _cert_manager;
  class _local_cache;
  class _server_database;
  class _server_http_api;
  class _storage_log;

  namespace file_manager {
    class file_manager_service;
  }


  class _server : public iserver
  {
  public:
    _server(server * owner);
    ~_server();
    
    void start(const service_provider &);
    void stop(const service_provider &);
    async_task<> prepare_to_stop(const service_provider &sp);

  private:
    friend class server;
    friend class iserver;

    server * owner_;
    certificate certificate_;
    asymmetric_private_key private_key_;

    std::unique_ptr<_node_manager> node_manager_;
    std::unique_ptr<user_manager> user_manager_;
	  std::unique_ptr<class db_model> db_model_;
    std::unique_ptr<_server_http_api> server_http_api_;
    std::unique_ptr<_storage_log> storage_log_;
    std::unique_ptr<_chunk_manager> chunk_manager_;
    std::unique_ptr<_server_database> server_database_;
    std::unique_ptr<_local_cache> local_cache_;
    std::unique_ptr<class p2p_network> p2p_network_;
    std::shared_ptr<class p2p_network_client> network_client_;

    std::unique_ptr<class log_sync_service> log_sync_service_;
    std::unique_ptr<file_manager::file_manager_service> file_manager_;

  public:
    leak_detect_helper leak_detect_;

    void get_statistic(server_statistic &result);
  };
}

#endif // __VDS_SERVER_SERVER_P_H_
