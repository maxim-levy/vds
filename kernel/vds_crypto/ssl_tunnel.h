//#ifndef __VDS_CRYPTO_SSL_TUNNEL_H_
//#define __VDS_CRYPTO_SSL_TUNNEL_H_
///*
//Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
//All rights reserved
//*/
//
//#include "async_buffer.h"
//
//namespace vds {
//  class certificate;
//  class asymmetric_private_key;
//
//  class _ssl_tunnel;
//  class ssl_tunnel {
//  public:
//    ssl_tunnel(
//      
//      const stream_output_async<uint8_t> & crypted_output,
//      const stream_output_async<uint8_t> & decrypted_output,
//      bool is_client,
//      const certificate * cert,
//      const asymmetric_private_key * key
//    );
//
//    ~ssl_tunnel();
//    
//    bool is_client() const;
//
//    vds::stream_output_async<uint8_t> & crypted_input();
//    vds::stream_output_async<uint8_t> & decrypted_input();
//
//    void start();
//    certificate get_peer_certificate() const;
//
//    void on_error(const std::function<void(const std::exception_ptr &)> & handler);
//
//  private:
//    std::shared_ptr<_ssl_tunnel> impl_;
//  };
//}
//
//#endif//__VDS_CRYPTO_SSL_TUNNEL_H_
