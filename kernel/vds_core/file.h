#ifndef __VDS_CORE_FILE_H_
#define __VDS_CORE_FILE_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "filename.h"
#include "sequence.h"

namespace vds {

  class file
  {
  public:
    enum file_mode
    {
      //Opens the file if it exists and seeks to the end of the file, or creates a new file.
      append,

      //Specifies that the operating system should open an existing file
      open_read,

      //Specifies that the operating system should open an existing file
      open_write,

      //Specifies that the operating system should open an existing file
      open_read_and_write,

      //Specifies that the operating system should open a file if it exists; otherwise, a new file should be created
      open_or_create,

      //Specifies that the operating system should create a new file
      create,

      //Specifies that the operating system should create a new file
      create_new,

      //Specifies that the operating system should open an existing file
      truncate
    };


    file();
    file(const filename & filename, file_mode mode);
    ~file();

    void open(const filename & filename, file_mode mode);
    void close();
    
    size_t read(void * buffer, size_t buffer_len);
    void write(const void * buffer, size_t buffer_len);

    size_t length() const;

    static size_t length(const filename & fn);
    static bool exists(const filename & fn);

    static void move(const filename & source, const filename & target);
    static void delete_file(const filename & fn);
    static std::string read_all_text(const filename & fn);

  private:
    filename filename_;
    int handle_;
  };

  class output_text_stream
  {
  public:
    output_text_stream(file & f);
    ~output_text_stream();

    void write(const std::string & value);
    void flush();

  private:
    file & f_;
    char buffer_[4096];
    size_t written_;
  };

  class input_text_stream
  {
  public:
    input_text_stream(file & f);

    bool read_line(std::string & result);

  private:
    file & f_;
    char buffer_[4096];
    size_t offset_;
    size_t readed_;
  };

  class read_file_stream
  {
  public:
    read_file_stream(const filename & filename)
      : filename_(filename)
    {
#ifndef _WIN32
      this->handle_ = open(filename.local_name().c_str(), O_RDONLY);
      if (0 > this->handle_) {
        auto error = errno;
        throw new std::system_error(error, std::system_category(), "Unable to open file " + this->filename_.str());
      }
#else
      this->handle_ = _open(this->filename_.local_name().c_str(), _O_RDONLY | _O_BINARY | _O_SEQUENTIAL);
      if (0 > this->handle_) {
        auto error = GetLastError();
        throw new std::system_error(error, std::system_category(), "Unable to open file " + this->filename_.str());
      }
#endif
    }

    ~read_file_stream()
    {
#ifndef _WIN32
      close(this->handle_);
#else
      _close(this->handle_);
#endif
    }

    template<typename next_step_t>
    bool read(next_step_t & next)
    {
      auto readed = ::read(this->handle_, this->buffer_, sizeof(this->buffer_));
      if (0 > readed) {
#ifdef _WIN32
        auto error = GetLastError();
#else
        auto error = errno;
#endif
        throw new std::system_error(error, std::system_category(), "Unable to read file " + this->filename_.str());
      }

      if (0 < readed) {
        next(this->buffer_, readed);
      }
      
      return (0 < readed);
    }

  private:
    filename filename_;
    int handle_;
    char buffer_[4096];
  };

  class read_file
  {
  public:
    read_file(const filename & filename)
      : filename_(filename)
    {
    }

    template <typename context_type>
    class handler : public sequence_step<context_type, void(const void *, size_t)>
    {
      using base_class = sequence_step<context_type, void(const void *, size_t)>;
    public:
      handler(
        const context_type & context,
        const read_file & args
        ) : base_class(context),
        filename_(args.filename_)
      {
#ifndef _WIN32
        this->handle_ = open(this->filename_.local_name().c_str(), O_RDONLY);
        if (0 > this->handle_) {
          auto error = errno;
          throw new std::system_error(error, std::system_category(), "Unable to open file " + this->filename_.str());
        }
#else
        this->handle_ = _open(this->filename_.local_name().c_str(), _O_RDONLY | _O_BINARY | _O_SEQUENTIAL);
        if (0 > this->handle_) {
          auto error = GetLastError();
          throw new std::system_error(error, std::system_category(), "Unable to open file " + this->filename_.str());
        }
#endif
      }

      ~handler()
      {
        if (0 < this->handle_) {
#ifndef _WIN32
          close(this->handle_);
#else
          _close(this->handle_);
#endif
        }
      }

      void operator()()
      {
        this->processed();
      }

      void processed()
      {
        auto readed = ::read(this->handle_, this->buffer_, sizeof(this->buffer_));
        if (0 > readed) {
#ifdef _WIN32
          auto error = GetLastError();
#else
          auto error = errno;
#endif
          throw new std::system_error(error, std::system_category(), "Unable to read file " + this->filename_.str());
        }

        if (0 < readed) {
          this->next(this->buffer_, readed);
        }
        else {
          this->next(nullptr, 0);
        }
      }

    private:
      filename filename_;
      int handle_;
      char buffer_[4096];
    };

  private:
    filename filename_;
  };
  
  class write_file
  {
  public:
    write_file(
      const filename & filename,
      file::file_mode mode)
    : filename_(filename),
    mode_(mode)
    {
    }
    
    template <typename context_type>
    class handler : public sequence_step<context_type, void(void)>
    {
      using base_class = sequence_step<context_type, void(void)>;
    public:
      handler(
        const context_type & context,
        const write_file & args
        ) : base_class(context),
        f_(args.filename_, args.mode_)
      {
      }
      
      void operator()(const void * data, size_t size)
      {
        if(0 == size){
          this->f_.close();
          this->next();
        }
        else {
          this->f_.write(data, size);
          this->prev();
        }
      }
      
    private:
      file f_;
    };
    
  private:
    filename filename_;
    file::file_mode mode_;
  };
}

#endif//__VDS_CORE_FILE_H_
