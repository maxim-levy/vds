/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "stdafx.h"
#include "file_manager.h"
#include "file_manager_p.h"
#include "connection_manager.h"
#include "log_records.h"
#include "chunk_manager.h"
#include "storage_log.h"
#include "server_database.h"

vds::async_task<>
vds::file_manager::put_file(
  const service_provider & sp,
  const std::string & version_id,
  const std::string & user_login,
  const std::string & name,
  const const_data_buffer & meta_info,
  const filename & fn)
{
  return static_cast<_file_manager *>(this)->put_file(sp, version_id, user_login, name, meta_info, fn);
}

vds::async_task<vds::const_data_buffer /*meta_info*/, vds::filename> vds::file_manager::download_file(
  const service_provider & sp,
  const std::string & user_login,
  const std::string & name)
{
  return static_cast<_file_manager *>(this)->download_file(sp, user_login, name);
}

vds::async_task<vds::const_data_buffer /*meta_info*/, vds::filename> vds::file_manager::download_file(
  const service_provider & sp,
  const guid & server_id,
  const std::string & version_id)
{
  return static_cast<_file_manager *>(this)->download_file(sp, server_id, version_id);
}

///////////////////////////////////////////////////////////////////////////
vds::_file_manager::_file_manager()
{
}


vds::async_task<>
vds::_file_manager::put_file(
  const service_provider & sp,
  const std::string & version_id,
  const std::string & user_login,
  const std::string & name,
  const const_data_buffer & meta_info,
  const filename & fn)
{
  return create_async_task(
    [this, fn, version_id, user_login, name, meta_info](
      const std::function<void(const service_provider & sp)> & done,
      const error_handler & on_error,
      const service_provider & sp) {

      server_log_file_map result(version_id, user_login, name, meta_info);

      sp.get<ichunk_manager>()->add(sp, result, fn).wait(
          [this, done, &result](const service_provider & sp) {
            sp.get<istorage_log>()->add_to_local_log(sp, result.serialize());
            done(sp);
          },
          on_error,
          sp);
  });
}

//vds::async_task<> vds::_storage_log::save_file(
//  const std::string & version_id,
//  const std::string & user_login,
//  const std::string & name,
//  const filename & tmp_file)
//{
//  return this->chunk_manager_
//    .get(this->sp_)
//    .add(version_id, user_login, name, tmp_file)
//    .then([this](
//      const std::function<void(void)> & done,
//      const error_handler & on_error,
//      const server_log_file_map & fm) {
//    this->add_to_local_log(fm.serialize().get());
//    done();
//  });

vds::async_task<vds::const_data_buffer /*meta_info*/, vds::filename> vds::_file_manager::download_file(
  const service_provider & sp,
  const std::string & user_login,
  const std::string & name)
{
  std::list<server_log_file_version> versions;
  sp.get<iserver_database>()->get_file_versions(sp, user_login, name, versions);

  if (versions.empty()) {
    throw std::runtime_error("File not found");
  }

  auto top_version = *versions.begin();
  return this->download_file(sp, top_version.server_id(), top_version.version_id());
}


class get_file_results : public std::enable_shared_from_this<get_file_results>
{
public:
  get_file_results(size_t count)
    : count_(count)
  {
  }

  void get_object_done(const vds::service_provider & sp, uint64_t index, const vds::filename & fn)
  {
    std::lock_guard<std::mutex> lock(this->data_mutex_);
    this->results_[index] = fn;

    this->try_complete(sp);
  }

  void get_object_error(const vds::service_provider & sp, std::exception_ptr ex)
  {
    std::lock_guard<std::mutex> lock(this->data_mutex_);
    this->errors_.push_back(ex);

    this->try_complete(sp);
  }

  void wait(
    const vds::service_provider & sp,
    const std::function<void(const vds::service_provider & sp, std::map<uint64_t, vds::filename> & result)> & done,
    const vds::error_handler & on_error)
  {
    std::lock_guard<std::mutex> lock(this->data_mutex_);
    this->done_ = done;
    this->on_error_ = on_error;

    this->try_complete(sp);
  }

private:
  const size_t count_;

  std::mutex data_mutex_;

  std::map<uint64_t, vds::filename> results_;
  std::list<std::exception_ptr> errors_;

  std::function<void(const vds::service_provider & sp, std::map<uint64_t, vds::filename> & result)> done_;
  vds::error_handler on_error_;

  void try_complete(const vds::service_provider & sp)
  {
    if (this->count_ == this->results_.size() + this->errors_.size()) {
      if (!this->done_) {
        return;
      }

      if (!this->errors_.empty()) {
        this->on_error_(sp, *this->errors_.begin());
      }
      else {
        this->done_(sp, this->results_);
      }
    }
  }
};


vds::async_task<vds::const_data_buffer /*meta_info*/, vds::filename> vds::_file_manager::download_file(
  const service_provider & sp,
  const guid & server_id,
  const std::string & version_id)
{
  auto file_map = sp.get<iserver_database>()->get_file_version_map(sp, server_id, version_id);

  if (!file_map) {
    throw std::runtime_error("File '" + version_id + "' not found");
  }

  auto result = std::make_shared<get_file_results>(file_map->items().size());
  for (auto & index : file_map->items()) {
    auto fn = sp.get<ilocal_cache>()->get_object_filename(sp, server_id, index.index());

    if (file::exists(fn)) {
      result->get_object_done(sp, index.index(), fn);
    }
    else {
      sp.get<iconnection_manager>()
        ->download_object(sp, server_id, index.index(), fn)
        .wait(
          [result, index, fn](const service_provider & sp) { result->get_object_done(sp, index.index(), fn); },
          [result](const service_provider & sp, std::exception_ptr ex) {result->get_object_error(sp, ex); },
          sp
      );
    }
  }

  return create_async_task([this, result, file_map = std::shared_ptr<server_log_file_map>(std::move(file_map)), version_id](
    const std::function<void(const service_provider & sp, const_data_buffer meta_info, vds::filename)> & done,
    const error_handler & on_error,
    const service_provider & sp) {
    result->wait(
      sp,
      [this, done, on_error, file_map, version_id](const service_provider & sp, std::map<uint64_t, vds::filename> & result) {
        foldername tmp(persistence::current_user(sp), "tmp");
        tmp.create();

        filename tmpfile(tmp, version_id);
        file f(tmpfile, file::file_mode::create_new);

        for (auto & index : file_map->items()) {
          barrier b;
          dataflow(
            file_read(result[index.index()]),
            inflate(),
            file_append(f))(
              [&b](const service_provider & sp) { b.set(); },
              [&b, on_error](const service_provider & sp, std::exception_ptr ex) { b.set(); on_error(sp, ex); },
              sp);

          b.wait();
        }

        f.close();

        done(sp, file_map->meta_info(), tmpfile);
      },
      on_error);
  });
}

