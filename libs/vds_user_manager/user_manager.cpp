/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/
#include <certificate_unknown_dbo.h>
#include "stdafx.h"
#include "user_manager.h"
#include "member_user.h"
#include "user_manager_storage.h"
#include "private/user_manager_p.h"
#include "private/member_user_p.h"
#include "database_orm.h"
#include "transactions/root_user_transaction.h"
#include "transactions/create_channel_transaction.h"
#include "transactions/channel_add_member_transaction.h"
#include "transactions/channel_add_message_transaction.h"
#include "private/cert_control_p.h"
#include "transaction_context.h"
#include "cert_control.h"
#include "transactions/device_user_add_transaction.h"
#include "run_configuration_dbo.h"
#include "vds_exceptions.h"
#include "transactions/channel_create_transaction.h"
#include "transactions/channel_add_reader_transaction.h"
#include "transactions/channel_add_writer_transaction.h"
#include "transactions/user_channel_create_transaction.h"
#include "transaction_log.h"
#include "db_model.h"
#include "certificate_chain_dbo.h"

vds::user_manager::user_manager()
{
}

void vds::user_manager::load(
	const service_provider & sp,
	database_transaction & t,
  const guid & config_id)
{
	if (nullptr != this->impl_.get()) {
		throw std::runtime_error("Logic error");
	}

	dbo::run_configuration t1;
	auto st = t.get_reader(
    t1
    .select(t1.cert, t1.cert_private_key, t1.port)
    .where(t1.id == config_id));
	if (!st.execute()) {
		throw std::runtime_error("Unable to get current configuration");
	}

	this->impl_.reset(new _user_manager(
    config_id,
    certificate::parse_der(t1.cert.get(st)),
    asymmetric_private_key::parse_der(t1.cert_private_key.get(st), std::string()),
    safe_cast<uint16_t>(t1.port.get(st))));

	this->impl_->load(sp, t);
}

vds::device_activation vds::user_manager::reset(
    const service_provider &sp,
    database_transaction &t,
    const std::string &root_user_name,
    const std::string &root_password,
    const asymmetric_private_key &root_private_key,
    const std::string &device_name,
    int port) {

  guid common_channel_id = guid::new_guid();
  transactions::transaction_block playback;
  //Create root user
  auto root_user = this->create_root_user(playback, t, common_channel_id, root_user_name, root_password,
                                          root_private_key);

  sp.get<logger>()->info(ThisModule, sp, "Create root user %s. Cert %s", 
	  root_user.id().str().c_str(),
	  cert_control::get_id(root_user.user_certificate()).str().c_str());

  //Lock to device
  std::list<certificate> certificate_chain;
  certificate_chain.push_back(root_user.user_certificate());
  auto device_key = asymmetric_private_key::generate(asymmetric_crypto::rsa4096());
  auto device_user = this->lock_to_device(
      sp, t, playback,
	  certificate_chain,
	  root_user, root_user_name, root_password, root_private_key,
      device_name, device_key, port);

  playback.add(
			root_user.id(),
      transactions::device_user_add_transaction(
          device_user.id(),
          device_user.user_certificate()));

  auto blocks = playback.save(
      sp,
      t,
      [&root_user, &root_private_key](
          const guid & channel_id,
          certificate & read_cert,
          certificate & write_cert,
          asymmetric_private_key & write_private_key) {
        if(root_user.id() == channel_id){
          read_cert = root_user.user_certificate();
          write_cert = root_user.user_certificate();
          write_private_key = root_private_key;
        }
        else {
          throw std::runtime_error("Invalid channel id");
        }
      });
  this->load(sp, t, device_user.id());

  return device_activation(root_user_name, certificate_chain, root_private_key);
}

vds::async_task<> vds::user_manager::init_server(
	const service_provider & parent_sp,
	const device_activation & request,
	const std::string & user_password,
	const std::string & device_name,
	int port)
{
	auto sp = parent_sp.create_scope(__FUNCTION__);
	return sp.get<db_model>()->async_transaction(sp, [this, sp, request, user_password, device_name, port](database_transaction & t)
	{
    auto user = this->import_user(*request.certificate_chain().rbegin());
		transactions::transaction_block log;

		auto private_key = asymmetric_private_key::generate(asymmetric_crypto::rsa4096());
		auto device_user = this->lock_to_device(
			sp,
			t,
			log,
			request.certificate_chain(),
			user,
			request.user_name(),
			user_password,
			request.private_key(),
			device_name, 
			private_key,
			port);

		log.add(
      user.id(),
			transactions::device_user_add_transaction(
				device_user.id(),
				device_user.user_certificate()));

		auto blocks = log.save(
			sp, t,
      [&user, &request](const guid & channel_id,
         certificate & read_cert,
         certificate & write_cert,
         asymmetric_private_key & write_private_key){
        if(user.id() == channel_id){
          read_cert = user.user_certificate();
          write_cert = user.user_certificate();
          write_private_key = request.private_key();
        }
        else {
          throw std::runtime_error("Invalid channel");
        }
      });
		this->load(sp, t, device_user.id());

		return true;
	});
}

vds::user_channel
vds::user_manager::create_channel(
	const service_provider & sp,
	transactions::transaction_block &log,
	database_transaction &t,
    const vds::guid &channel_id,
	const std::string &name,
    const vds::guid &owner_id,
	const certificate &owner_cert,
    const asymmetric_private_key &owner_private_key,
	asymmetric_private_key &read_private_key,
	asymmetric_private_key &write_private_key) const {

	auto read_id = vds::guid::new_guid();
	auto write_id = vds::guid::new_guid();

	sp.get<logger>()->info(ThisModule, sp, "Create channel %s(%s). Read cert %s. Write cert %s",
		channel_id.str().c_str(),
		name.c_str(),
		read_id.str().c_str(),
		write_id.str().c_str());

  read_private_key = vds::asymmetric_private_key::generate(vds::asymmetric_crypto::rsa4096());
  auto read_cert = vds::_cert_control::create_cert(
      read_id,
      "Read Member Certificate " + read_id.str() + " for channel " + channel_id.str(),
      read_private_key,
      owner_cert,
      owner_private_key);

  write_private_key = vds::asymmetric_private_key::generate(vds::asymmetric_crypto::rsa4096());
  auto write_cert = vds::_cert_control::create_cert(
      write_id,
      "Write Member Certificate " + write_id.str() + " for channel " + channel_id.str(),
      write_private_key,
      owner_cert,
      owner_private_key);

  log.add(
      owner_id,
      transactions::channel_create_transaction(
          channel_id,
          name,
          read_cert,
          read_private_key,
          write_cert,
          write_private_key));

  return user_channel(channel_id, name, read_cert, write_cert);
}

vds::member_user
vds::user_manager::lock_to_device(
	const vds::service_provider &sp,
	vds::database_transaction &t,
	transactions::transaction_block & playback,
	const std::list<certificate> & certificate_chain,
                                  const member_user &user,
								  const std::string &user_name,
                                  const std::string &user_password,
                                  const asymmetric_private_key &user_private_key,
                                  const std::string &device_name,
                                  const asymmetric_private_key &device_private_key,
                                  int port) {

  auto device_user = user.create_device_user(
      user_private_key,
      device_private_key,
      device_name);

  dbo::run_configuration t3;
  t.execute(
      t3.insert(
          t3.id = device_user.id(),
          t3.cert = device_user.user_certificate().der(),
          t3.cert_private_key = device_private_key.der(std::string()),
          t3.port = port));
  orm::certificate_chain_dbo t4;
  for(auto & cert : certificate_chain)
  {
	  t.execute(
		  t4.insert(
			  t4.id = cert_control::get_id(cert),
			  t4.cert = cert.der(),
			  t4.parent = cert_control::get_parent_id(cert)));
  }
  t.execute(
	  t4.insert(
		  t4.id = cert_control::get_id(device_user.user_certificate()),
		  t4.cert = device_user.user_certificate().der(),
		  t4.parent = cert_control::get_parent_id(device_user.user_certificate())));

  return device_user;
}

vds::member_user vds::user_manager::import_user(const certificate &user_cert) {
  return vds::member_user::import_user(user_cert);
}

vds::member_user vds::user_manager::get_current_device(
    const vds::service_provider &sp,
    asymmetric_private_key &device_private_key) const {

	return this->impl_->get_current_device(sp, device_private_key);
}

vds::certificate vds::user_manager::get_channel_write_cert(
  const service_provider & sp,
  const guid & channel_id) const
{
	return this->impl_->get_channel_write_cert(channel_id);
}


vds::asymmetric_private_key vds::user_manager::get_channel_write_key(
  const service_provider & sp,
  const guid & channel_id) const
{
	return this->impl_->get_channel_write_key(channel_id);
}

vds::asymmetric_private_key vds::user_manager::get_channel_write_key(
		const service_provider & sp,
		const guid & channel_id,
		const guid & cert_id) const
{
	return this->impl_->get_channel_write_key(channel_id, cert_id);
}

vds::certificate vds::user_manager::get_channel_read_cert(
  const service_provider & sp,
  const guid & channel_id) const
{
	return this->impl_->get_channel_read_cert(channel_id);
}

vds::asymmetric_private_key vds::user_manager::get_channel_read_key(
  const service_provider & sp,
  const guid & channel_id) const
{
	return this->impl_->get_channel_read_key(channel_id);
}

vds::asymmetric_private_key vds::user_manager::get_channel_read_key(
		const service_provider & sp,
		const guid & channel_id,
		const guid & cert_id) const
{
	return this->impl_->get_channel_read_key(channel_id, cert_id);
}

vds::certificate vds::user_manager::get_certificate(
  const service_provider & sp,
  const guid & cert_id) const
{
	return this->impl_->get_certificate(cert_id);
}


vds::user_channel vds::user_manager::get_channel(
  const service_provider & sp,
  const guid & channel_id) const
{
	return this->impl_->get_channel(channel_id);
}

vds::member_user vds::user_manager::create_root_user(transactions::transaction_block &playback, database_transaction &t,
                                                     const guid &common_channel_id, const std::string &root_user_name,
                                                     const std::string &root_password,
                                                     const vds::asymmetric_private_key &root_private_key) {
  auto root_user_id = guid::new_guid();
  auto root_user_cert = _cert_control::create_root(
      root_user_id,
      "User " + root_user_name,
      root_private_key);

  playback.add(
      root_user_id,
      transactions::root_user_transaction(
          root_user_id,
          root_user_cert,
          root_user_name,
          root_private_key.der(root_password),
          hash::signature(hash::sha256(), root_password.c_str(), root_password.length())));

  return member_user(new _member_user(root_user_id, root_user_cert));
}

vds::certificate vds::user_manager::get_channel_write_cert(
		const vds::service_provider &sp,
		const vds::guid &channel_id,
		const vds::guid &cert_id) const {
	return this->impl_->get_channel_write_cert(channel_id, cert_id);
}

bool vds::user_manager::validate_and_save(
		const service_provider & sp,
		const std::list<vds::certificate> &cert_chain) {

  return this->impl_->validate_and_save(
      sp,
      cert_chain);
}

void vds::user_manager::save_certificate(
    const vds::service_provider &sp,
    vds::database_transaction &t,
    const vds::certificate &cert) {

  orm::certificate_chain_dbo t1;
  t.execute(
      t1.insert(
          t1.id = cert_control::get_id(cert),
      t1.cert = cert.der(),
      t1.parent = cert_control::get_parent_id(cert)));

  orm::certificate_unknown_dbo t2;
  t.execute(t2.delete_if(t2.id == cert_control::get_id(cert)));
}


////////////////////////////////////////////////////////////////////////
vds::_user_manager::_user_manager(
  const guid& id,
  const certificate& device_cert,
  const asymmetric_private_key& device_private_key,
  const uint16_t port)
	: security_walker(
		cert_control::get_user_id(device_cert),
		device_cert,
		device_private_key),
		id_(id),
		device_cert_(device_cert),
		device_private_key_(device_private_key),
		port_(port) {
}

vds::member_user vds::_user_manager::get_current_device(const service_provider & sp, asymmetric_private_key & device_private_key) const
{
	device_private_key = this->user_private_key();
	return member_user(
		new _member_user(
			this->user_id(),
			this->user_cert()));
}

vds::user_channel vds::_user_manager::get_channel(const guid & channel_id) const
{
	return user_channel(
		channel_id,
		this->get_channel_name(channel_id),
		this->get_channel_read_cert(channel_id),
		this->get_channel_write_cert(channel_id));
}

bool vds::_user_manager::validate_and_save(
    const service_provider & sp,
    const std::list<vds::certificate> &cert_chain) {

  certificate_store store;
  for(const auto & p : cert_chain){
    auto cert = this->get_certificate(cert_control::get_id(p));
    if(!cert) {
      cert = p;

      const auto result = store.verify(cert);
      if (0 != result.error_code) {
        sp.get<logger>()->warning(ThisModule, sp, "Invalid certificate %s %s",
                                  result.error.c_str(),
                                  result.issuer.c_str());
        return false;
      }
    }

    store.add(cert);
    this->save_certificate(sp, cert);
  }

  return true;
}

void vds::_user_manager::save_certificate(const vds::service_provider &sp, const vds::certificate &cert) {
  security_walker::add_certificate(cert);

  sp.get<db_model>()->async_transaction(sp, [cert](database_transaction & t)->bool{

    orm::certificate_chain_dbo t1;
    auto st = t.get_reader(t1.select(t1.id).where(t1.id == cert_control::get_id(cert)));
    if(!st.execute()){
      t.execute(t1.insert(
          t1.id = cert_control::get_id(cert),
          t1.cert = cert.der(),
          t1.parent = cert_control::get_parent_id(cert)));
    }

    return true;
  }).execute([sp](const std::shared_ptr<std::exception> & ex){
    if(ex) {
      sp.get<logger>()->warning(ThisModule, sp, "%s at saving certificate", ex->what());
    }
  });
}
