//
// Created by vadim on 31.10.17.
//
#include "stdafx.h"
#include "udp_transport.h"
#include "private/udp_transport_p.h"
#include "binary_serialize.h"
#include "udp_socket.h"
#include "udp_datagram_size_exception.h"
#include "url_parser.h"
#include "network_service.h"
#include "vds_debug.h"

///////////////////////////////////////////////////////
vds::udp_transport::udp_transport() {

}

vds::udp_transport::~udp_transport() {

}

void
vds::udp_transport::start(
    const vds::service_provider &sp,
    int port,
    const new_session_handler_t &new_session_handler) {
  this->impl_.reset(new _udp_transport(new_session_handler));
  this->impl_->start(sp, port);
}

void vds::udp_transport::stop(const vds::service_provider &sp) {
  this->impl_->stop(sp);
}

void vds::udp_transport::connect(const vds::service_provider &sp, const std::string &address) {
  this->impl_->connect(sp, address);
}

vds::async_task<> vds::udp_transport::prepare_to_stop(const vds::service_provider &sp) {
  return this->impl_->prepare_to_stop(sp);
}

vds::async_task<> vds::udp_transport::session::prepare_to_stop(const vds::service_provider &sp) {
  return this->impl_->prepare_to_stop(sp);
}

///////////////////////////////////////////////////////
vds::_udp_transport::_udp_transport(const udp_transport::new_session_handler_t & new_session_handler)
: instance_id_(guid::new_guid()),
  new_session_handler_(new_session_handler),
  send_queue_(new _udp_transport_queue()),
  timer_("UDP transport timer"),
	is_closed_(false)
{
}

vds::_udp_transport::~_udp_transport() {

}

void vds::_udp_transport::start(const service_provider &sp, int port) {
  this->server_.start(sp, "127.0.0.1", port);

  this->read_message(sp);

  this->timer_.start(sp, std::chrono::seconds(1), [sp, pthis = this->shared_from_this()]()->bool{
    return pthis->do_timer_tasks(sp);
  });

}

void vds::_udp_transport::send_broadcast(int port) {

  this->server_.socket().send_broadcast(port, this->create_handshake_message());

}

vds::const_data_buffer vds::_udp_transport::create_handshake_message() {
  if(16 != this->instance_id_.size()){
    throw std::runtime_error("Invalid GUID size");
  }

  uint8_t data[20];
  *(reinterpret_cast<uint32_t *>(data)) = htonl(
      ((uint32_t)_udp_transport_session::control_type::Handshake) << 24
      | udp_transport::protocol_version);
  memcpy(data + 4, this->instance_id_.data(), this->instance_id_.size());

  return const_data_buffer(data, sizeof(data));
}

void vds::_udp_transport::process_incommig_message(
    const service_provider & sp,
    const udp_datagram & message) {
  std::unique_lock<std::shared_mutex> lock(this->sessions_mutex_);
  if (this->is_closed_) {
	  return;
  }

  _udp_transport_session_address_t server_address(message.server(), message.port());

  std::shared_ptr<_udp_transport_session> session;
  auto p = this->sessions_.find(server_address);
  if (this->sessions_.end() != p) {
    session = p->second;
  } else {
    session = std::make_shared<_udp_transport_session>(
        this->shared_from_this(),
        server_address);
    this->sessions_[server_address] = session;
  }

  if(0x80 == (0x80 & static_cast<const uint8_t *>(message.data())[0])
     && _udp_transport_session::control_type::Handshake == (_udp_transport_session::control_type)(static_cast<const uint8_t *>(message.data())[0] >> 4)){

    if(4 > message.data_size()){
      session->close(sp, std::make_shared<std::runtime_error>("Invalid data"));
      return;
    }

    auto version = 0x0FFFFFFF & ntohl(*reinterpret_cast<const uint32_t *>(message.data()));

    if(version == udp_transport::protocol_version && 20 == message.data_size()){
      guid instance_id(message.data() + 4, 16);
      if(instance_id != this->instance_id_){
        if(0 != session->get_instance_id().size()) {
          session = std::make_shared<_udp_transport_session>(
              this->shared_from_this(),
              server_address);
          this->sessions_[server_address] = session;
        }
        sp.get<logger>()->trace("UDPAPI", sp, "%s: New session from %s",
                                instance_id.str().c_str(), this->instance_id_.str().c_str());
        session->set_instance_id(instance_id);
      }
      else {
        session->close(sp, std::make_shared<std::runtime_error>("Self"));
      }
    }
    else {
      session->close(sp, std::make_shared<std::runtime_error>("Invalid protocol version"));
    }
  }

  if(!session->is_failed()) {
    lock.unlock();
    try {
      session->incomming_message(sp, *this, message.data(), message.data_size());
    }
    catch (const std::exception & ex) {
      std::unique_lock<std::shared_mutex> lock(this->sessions_mutex_);
      session->close(sp, std::make_shared<std::runtime_error>(ex.what()));
    }
    catch (...) {
      std::unique_lock<std::shared_mutex> lock(this->sessions_mutex_);
      session->close(sp, std::make_shared<std::runtime_error>("Internal error"));
    }
  }
}

void vds::_udp_transport::send_data(
    const service_provider & sp,
    const std::shared_ptr<_udp_transport_session> & session,
    const const_data_buffer & data)
{
  this->send_queue_->send_data(sp, this->shared_from_this(), session, data);
}

/////////////////////////////////////////////////////////////////////////
void vds::_udp_transport::connect(const vds::service_provider &sp, const std::string & address) {
  auto na = url_parser::parse_network_address(address);
  if(na.protocol != "udp"){
    throw std::invalid_argument("address");
  }

  _udp_transport_session_address_t addr(na.server, (uint16_t)atoi(na.port.c_str()));

  std::unique_lock<std::shared_mutex> lock(this->sessions_mutex_);
  if(this->sessions_.end() != this->sessions_.find(addr)){
    return;
  }

  auto scope = sp.create_scope(("Connect to " + address).c_str());

  auto new_session = std::make_shared<_udp_transport_session>(this->shared_from_this(), addr);
  this->sessions_[addr] = new_session;
  new_session->send_handshake(scope, this->shared_from_this());
}

void vds::_udp_transport::stop(const vds::service_provider &sp) {

}

vds::async_task<> vds::_udp_transport::write_async(vds::udp_datagram &&message) {
  return this->server_.socket().write_async(std::move(message));
}

bool vds::_udp_transport::do_timer_tasks(const vds::service_provider &sp) {

  auto owner = this->shared_from_this();

  std::shared_lock<std::shared_mutex> lock(this->sessions_mutex_);
  for(auto & p : this->sessions_){
    p.second->on_timer(sp, owner);
  }

  return !sp.get_shutdown_event().is_shuting_down();
}

void vds::_udp_transport::read_message(const vds::service_provider &sp) {
  this->server_.socket().read_async().execute(
      [pthis = this->shared_from_this(), sp](
          const std::shared_ptr<std::exception> & ex,
          const udp_datagram & message){
        if(0 != message.data_size()) {
          if (!ex) {
            pthis->process_incommig_message(sp, message);
          }
          pthis->read_message(sp);
        }
      }
  );

}

void vds::_udp_transport::handshake_completed(
    const vds::service_provider &sp,
    _udp_transport_session *session) {
  this->new_session_handler_(
      udp_transport::session(session->shared_from_this()));
}

vds::async_task<> vds::_udp_transport::prepare_to_stop(const vds::service_provider &sp) {

  this->send_queue_->stop(sp);
  this->server_.stop(sp);

  std::unique_lock<std::shared_mutex> lock(this->sessions_mutex_);
  vds_assert(!this->is_closed_);
  this->is_closed_ = true;

  return [pthis = this->shared_from_this(), sp](const async_result<> & result){
    auto runner = new _async_series(result, pthis->sessions_.size());
    for (auto &session : pthis->sessions_) {
      runner->add(session.second->prepare_to_stop(sp));
    }
  };
}

