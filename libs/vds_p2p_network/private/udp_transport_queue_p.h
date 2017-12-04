#ifndef __VDS_P2P_NETWORK_UDP_TRANSPORT_QUEUE_H_
#define __VDS_P2P_NETWORK_UDP_TRANSPORT_QUEUE_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/
#include <queue>
#include "udp_transport_session_p.h"

namespace vds {
  class _udp_transport_queue : public std::enable_shared_from_this<_udp_transport_queue> {
  public:
    _udp_transport_queue();

    void send_data(
        const service_provider & sp,
        const std::shared_ptr<_udp_transport> & owner,
        const std::shared_ptr<_udp_transport_session> & session,
        const const_data_buffer & data);

  private:
    friend class _udp_transport_session;

    class datagram_generator {
    public:
      datagram_generator(const std::shared_ptr<_udp_transport_session> & owner)
          : owner_(owner) {
      }

      virtual ~datagram_generator() {
      }

      virtual uint16_t generate_message(
          uint8_t * buffer) = 0;

      virtual  void complete(
          const uint8_t * buffer, size_t len) = 0;

      virtual bool is_eof() const = 0;

      const std::shared_ptr<class _udp_transport_session> & owner(){
        return this->owner_;
      }

    private:
      std::shared_ptr<class _udp_transport_session> owner_;
    };

    class data_datagram : public datagram_generator {
    public:
      data_datagram(
          const std::shared_ptr<class _udp_transport_session> & owner,
          const const_data_buffer & data )
          : datagram_generator(owner), data_(data), offset_(0)
      {
      }

      //Generate message
      virtual uint16_t generate_message(
          uint8_t * buffer) override;

      //Store sent message
      void complete(const uint8_t * buffer, size_t len) override {
        this->owner()->add_datagram(const_data_buffer(buffer, len));
      }

      //
      bool is_eof() const override {
        return (this->offset_ >= this->data_.size());
      }

    private:
      const_data_buffer data_;
      uint16_t  offset_;
    };

    class handshake_datagram : public datagram_generator {
    public:
      handshake_datagram(
          const std::shared_ptr<class _udp_transport_session> & owner,
          const guid & instance_id)
          : datagram_generator(owner),
            instance_id_(instance_id)
      {
      }

      virtual uint16_t generate_message(
          uint8_t * buffer) override;

      void complete(const uint8_t * buffer, size_t len) override;

      bool is_eof() const override {
        return true;
      }

    private:
      guid instance_id_;
    };

    class welcome_datagram : public datagram_generator {
    public:
      welcome_datagram(
          const std::shared_ptr<class _udp_transport_session> & owner,
          const guid & instance_id)
          : datagram_generator(owner),
            instance_id_(instance_id)
      {
      }

      virtual uint16_t generate_message(
          uint8_t * buffer) override;

      void complete(const uint8_t * buffer, size_t len) override;

      bool is_eof() const override {
        return true;
      }

    private:
      guid instance_id_;
    };

    class acknowledgement_datagram : public datagram_generator {
    public:
      acknowledgement_datagram(
          const std::shared_ptr<_udp_transport_session> &owner)
          : datagram_generator(owner) {
      }

      virtual uint16_t generate_message(
          uint8_t *buffer) override;
      bool is_eof() const override {
        return true;
      }

      void complete(const uint8_t * buffer, size_t len) override {
      }
    };

    std::queue<std::unique_ptr<datagram_generator>> send_data_buffer_;
    std::mutex send_data_buffer_mutex_;

    static constexpr uint16_t max_datagram_size = 65507;
    uint8_t buffer_[max_datagram_size];

    void continue_send_data(
        const service_provider & sp,
        const std::shared_ptr<_udp_transport> & owner);

    void emplace(
        const service_provider & sp,
        const std::shared_ptr<_udp_transport> & owner,
        datagram_generator * item);
  };
}

#endif //__VDS_P2P_NETWORK_UDP_TRANSPORT_QUEUE_H_
