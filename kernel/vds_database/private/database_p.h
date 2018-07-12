#ifndef __VDS_DATABASE_DATABASE_P_H_
#define __VDS_DATABASE_DATABASE_P_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include <chrono>
#include <thread>

#include "sqllite3/sqlite3.h"
#include "thread_apartment.h"

namespace vds {
  class database;

  class _sql_statement
  {
  public:
    _sql_statement(sqlite3 * db, const char * sql)
      : db_(db), stmt_(nullptr), state_(bof_state)
    {
      auto result = sqlite3_prepare_v2(db, sql, -1, &this->stmt_, nullptr);
      switch (result) {
      case SQLITE_OK:
        return;

      default:
        auto error = sqlite3_errmsg(db);
        throw std::runtime_error(error);
      }
    }

    ~_sql_statement()
    {
      if (nullptr != this->stmt_) {
        sqlite3_finalize(this->stmt_);
      }
    }

    void set_parameter(int index, int value)
    {
      this->reset();

      sqlite3_bind_int(this->stmt_, index, value);
    }

    void set_parameter(int index, uint64_t value)
    {
      this->reset();
      
      sqlite3_bind_int64(this->stmt_, index, value);
    }

    void set_parameter(int index, const std::string & value)
    {
      this->reset();
      
      sqlite3_bind_text(this->stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
    }

    void set_parameter(int index, const std::chrono::system_clock::time_point & value)
    {
      this->reset();

      sqlite3_bind_int64(this->stmt_, index, std::chrono::system_clock::to_time_t(value));
    }


    void set_parameter(int index, const const_data_buffer & value)
    {
      this->reset();
      
      sqlite3_bind_blob(this->stmt_, index, value.data(), (int)value.size(), SQLITE_TRANSIENT);
    }

    bool execute()
    {
      auto result = sqlite3_step(this->stmt_);
      switch (result) {
      case SQLITE_ROW:
        this->state_ = read_state;
        return true;

      case SQLITE_DONE:
        this->state_ = eof_state;
        return false;

      default:
        auto error = sqlite3_errmsg(this->db_);
        throw std::runtime_error(error);
      }
    }

    bool get_value(int index, int & value)
    {
      assert(read_state == this->state_);
      assert(0 <= index && index < sqlite3_column_count(this->stmt_));

      value = sqlite3_column_int(this->stmt_, index);
      return true;
    }

    bool get_value(int index, uint64_t & value)
    {
      assert(read_state == this->state_);
      assert(0 <= index && index < sqlite3_column_count(this->stmt_));

      value = sqlite3_column_int64(this->stmt_, index);
      return true;
    }

    bool get_value(int index, std::string & value)
    {
      assert(read_state == this->state_);
      assert(0 <= index && index < sqlite3_column_count(this->stmt_));

      auto v = (const char *)sqlite3_column_text(this->stmt_, index);
      if(nullptr == v){
        return false;
      }
      
      value = v;
      return true;
    }

    bool get_value(int index, const_data_buffer & value)
    {
      assert(read_state == this->state_);
      assert(0 <= index && index < sqlite3_column_count(this->stmt_));

      auto size = sqlite3_column_bytes(this->stmt_, index);
      if (0 >= size) {
        value.reset(nullptr, 0);
        return false;
      }
      else {
        auto v = sqlite3_column_blob(this->stmt_, index);
        value.reset(v, size);
        return true;
      }
    }

    bool get_value(int index, double & value)
    {
      assert(read_state == this->state_);
      assert(0 <= index && index < sqlite3_column_count(this->stmt_));

      value = sqlite3_column_double(this->stmt_, index);
      return true;
    }

    bool get_value(int index, std::chrono::system_clock::time_point & value)
    {
      assert(read_state == this->state_);
      assert(0 <= index && index < sqlite3_column_count(this->stmt_));

      value = std::chrono::system_clock::from_time_t(
          sqlite3_column_int64(this->stmt_, index));
      return true;
    }

    bool is_null(int index) const {
      assert(0 <= index && index < sqlite3_column_count(this->stmt_));
      return (SQLITE_NULL == sqlite3_column_type(this->stmt_, index));
    }

  private:
    //service_provider sp_;
    sqlite3 * db_;
    sqlite3_stmt * stmt_;
    //logger log_;
    //std::string query_;
    
    enum state_enum
    {
      bof_state,
      read_state,
      eof_state,
    };
    
    state_enum state_;
    
    void reset()
    {
      if(bof_state != this->state_){
        sqlite3_reset(this->stmt_);
        this->state_ = bof_state;
      }
    }
  };


  class _database : public std::enable_shared_from_this<_database>
  {
  public:
    _database()
    : db_(nullptr),
      execute_queue_(new thread_apartment())
    {
    }

    ~_database()
    {
      this->close();
    }
    
    void open(const service_provider & sp, const filename & database_file)
    {
      auto error = sqlite3_open(database_file.local_name().c_str(), &this->db_);

      if (SQLITE_OK != error) {
        throw std::runtime_error(sqlite3_errmsg(this->db_));
      }

      error = sqlite3_busy_timeout(this->db_, 300000);
      if (SQLITE_OK != error) {
        throw std::runtime_error(sqlite3_errmsg(this->db_));
      }
    }

    void close()
    {
      if (nullptr != this->db_) {
        auto error = sqlite3_close(this->db_);

        if (SQLITE_OK != error) {
          auto error_msg = sqlite3_errmsg(this->db_);
          throw std::runtime_error(error_msg);
        }

        this->db_ = nullptr;
      }
    }

    void execute(const char * sql)
    {
      char * zErrMsg = nullptr;
      auto result = sqlite3_exec(this->db_, sql, nullptr, 0, &zErrMsg);
      switch (result) {
      case SQLITE_OK:
        return;

      default:
        if(nullptr != zErrMsg){
          std::string error_message(zErrMsg);
          sqlite3_free(zErrMsg);

          throw std::runtime_error(error_message);
        }
        else {
          throw std::runtime_error("Sqlite3 error " + std::to_string(result));
        }
      }
    }

    sql_statement parse(const char * sql)
    {
      return sql_statement(new _sql_statement(this->db_, sql));
    }

    void async_read_transaction(
      const service_provider & sp,
      const std::function<void(database_read_transaction & tr)> & callback) {
      this->execute_queue_->schedule(sp, [pthis = this->shared_from_this(), callback]() {
        database_read_transaction tr(pthis);
        callback(tr);
      });
    }

    void async_transaction(
      const service_provider & sp,
      const std::function<bool(database_transaction & tr)> & callback) {
      this->execute_queue_->schedule(sp, [pthis = this->shared_from_this(), callback]() {
        pthis->execute("BEGIN TRANSACTION");

        database_transaction tr(pthis);

        bool result;
        try {
          result = callback(tr);
        }
        catch (...) {
          pthis->execute("ROLLBACK TRANSACTION");
          throw;
        }

        if (result) {
          pthis->execute("COMMIT TRANSACTION");
        }
        else {
          pthis->execute("ROLLBACK TRANSACTION");
        }
      });
    }

    async_task<> prepare_to_stop(const service_provider & sp){
      return async_task<>::empty();
    }

  private:
    filename database_file_;
    sqlite3 * db_;    

    std::shared_ptr<thread_apartment> execute_queue_;
  };
}



#endif//__VDS_DATABASE_DATABASE_P_H_
