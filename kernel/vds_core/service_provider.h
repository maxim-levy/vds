#ifndef __VDS_CORE_SERVICE_PROVIDER_H_
#define __VDS_CORE_SERVICE_PROVIDER_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include <typeinfo>
#include <memory>
#include <functional>
#include <map>
#include <stack>
#include <list>
#include <mutex>
#include "shutdown_event.h"
#include "string_format.h"
#include "types.h"
#include "foldername.h"

namespace vds {
    class service_provider
    {
    public:
      service_provider()
      {
      }
      
      template <typename interface_type>
      interface_type get() const;

      template <typename interface_type>
      bool enum_collection(const std::function<bool(interface_type)> & visiter, bool throwIfEmpty = true) const;

      template <typename interface_type>
      std::list<interface_type> get_collection(bool throwIfEmpty = true)  const;

      service_provider create_scope() const;

      void on_complete(const std::function<void(void)> & done) const;

      shutdown_event & get_shutdown_event();
      const shutdown_event & get_shutdown_event() const;
      
    private:
      friend class iservice_provider_impl;
      friend class service_registrator_impl;
      friend class persistence;
      friend class mock_client;
      friend class mock_server;

      service_provider(class iservice_provider_impl * impl);
      service_provider(const std::shared_ptr<iservice_provider_impl> & impl);

      std::shared_ptr<iservice_provider_impl> impl_;

      foldername current_user_folder_;
      foldername local_machine_folder_;

      void set_root_folders(
        const foldername & current_user_folder,
        const foldername & local_machine_folder);

    };

    template <typename interface_type>
    class lazy_service
    {
    public:
      interface_type & get(const service_provider & sp)
      {
        if (!this->ptr_) {
          std::lock_guard<std::mutex> lock(this->ptr_mutex_);
          if (!this->ptr_) {
            this->ptr_.reset(new holder(sp));
          }
        }

        return this->ptr_->impl_;
      }

    private:
      std::mutex ptr_mutex_;

      struct holder
      {
        interface_type impl_;

        holder(const service_provider & sp)
          : impl_(sp.get<interface_type>())
        {
        }
      };
      std::unique_ptr<holder> ptr_;
    };


    class service_registrator;

    class iservice
    {
    public:
      virtual ~iservice();

      virtual void register_services(service_registrator &) = 0;
      virtual void start(const service_provider &) = 0;
      virtual void stop(const service_provider &) = 0;
    };

    class service_registrator
    {
    public:
      service_registrator();

      template <typename interface_type>
      void add_factory(const std::function<interface_type(const service_provider &, bool &)> & factory);

      template <typename interface_type>
      void add_collection_factory(const std::function<interface_type(const service_provider &)> & factory);

      void add(iservice & service);

      void shutdown();
      
      void set_root_folders(
        const foldername & current_user_folder,
        const foldername & local_machine_folder);

      service_provider build() const;

    private:
      std::shared_ptr<class service_registrator_impl> impl_;
      foldername current_user_folder_;
      foldername local_machine_folder_;
    };

    //////////////////////////////////////////////////////////////////////////////////
    class iservice_provider_impl : public std::enable_shared_from_this<iservice_provider_impl>
    {
    public:
        iservice_provider_impl();
        ~iservice_provider_impl();

        template <typename interface_type>
        interface_type get(const service_provider & sp);

        template <typename interface_type>
        bool enum_collection(const service_provider &, const std::function<bool(interface_type)> & visiter, bool throwIfEmpty = true);

        template <typename interface_type>
        std::list<interface_type> get_collection(const service_provider &, bool throwIfEmpty = true);

        service_provider create_scope();

        void on_complete(const std::function<void(void)> & done);
        virtual shutdown_event & get_shutdown_event() = 0;

    protected:
        friend class scopped_service_provider;

        class iservice_factory {
        public:
            virtual ~iservice_factory();
        };

        template <typename interface_type>
        class service_factory : public iservice_factory {
        public:
            service_factory(const std::function<interface_type(const service_provider &, bool & ) > & factory)
                : factory_(factory) {
            }

            interface_type get(const service_provider & sp, bool & is_scoped) {
                return this->factory_(sp, is_scoped);
            }

        private:
            std::function<interface_type(const service_provider &, bool & ) > factory_;
        };

        template <typename interface_type>
        class service_collection_factory : public iservice_factory {
        public:
            void add(const std::function<interface_type(const service_provider &) > & factory);
            bool enum_collection(
              const service_provider & sp,
              const std::function<bool(interface_type)> & visiter);

        private:
            std::list<std::function<interface_type(const service_provider &)>> factories_;
        };

        virtual iservice_factory * get_factory(size_t type) = 0;

        struct iobject_holder
        {
            virtual ~iobject_holder();
        };

        template <typename interface_type>
        struct object_holder : public iobject_holder
        {
            object_holder(const interface_type & v)
                : value(v)
            {
            }

            interface_type value;
        };

        std::recursive_mutex m_;
        std::map<size_t, iobject_holder *> scopped_objects_;
        std::list<std::function<void(void)>> done_handlers_;
    };
    /////////////////////////////////////////////////////////////////////////////////
    class scope_properties_holder : public std::enable_shared_from_this<scope_properties_holder>
    {
    public:
      
    private:
      friend class iscope_properties;
      
      class property_holder_base
      {
      public:
        virtual ~property_holder_base();
        
        virtual bool get_value(const void **) const = 0;
        virtual void set_value(const void *)  = 0;
      };
      
      template <typename property_type>
      class property_holder : public property_holder_base
      {
      public:
        property_holder(const property_type & value)
        : value_(value)
        {
        }
        
        bool get_value(const void ** result) const override
        {
          *reinterpret_cast<const property_type **>(result) = &this->value_;
          return true;
        }
        
        void set_value(const void * value) override
        {
          this->value_ = *reinterpret_cast<const property_type *>(value);
        }
        
      private:
        property_type value_;
      };
      
      template <typename property_type>
      class property_func_holder : public property_holder_base
      {
      public:
        property_func_holder(
          const std::function<bool (property_type & cache, const property_type ** result)> & value_func)
        : value_func_(value_func)
        {
        }
        
        void get_value(const void ** result) const override
        {
          return this->value_func_(this->value_, reinterpret_cast<const property_type **>(result));
        }
        
        void set_value(const void * value) override
        {
          throw new std::logic_error("Invalid usage of scope properties based on lambda");
        }
        
      private:
        property_type value_;
        std::function<bool (property_type & cache, const property_type ** result)> value_func_;
      };
      
      std::map<size_t, std::unique_ptr<property_holder_base>> holders_;
      
      template <typename property_type>
      void add_property(const property_type & property)
      {
        auto p = this->holders_.find(types::get_type_id<property_type>());
        if(this->holders_.end() == p){
          this->holders_[types::get_type_id<property_type>()].reset(
            new property_holder<property_type>(property));
        }
        else {
          p->second->set_value(&property);
        }
      }
      
      template <typename property_type>
      void add_property(const std::function<bool (property_type & cache, const property_type ** result)> & property_func)
      {
        auto p = this->holders_.find(types::get_type_id<property_type>());
        if(this->holders_.end() == p){
          this->holders_[types::get_type_id<property_type>()].reset(
            new property_func_holder<property_type>(property_func));
        }
        else {
          throw new std::logic_error("Invalid usage of scope properties based on lambda");
        }
      }
      
      template <typename property_type>
      bool get_property(const property_type ** result)
      {
        auto p = this->holders_.find(types::get_type_id<property_type>());
        if(this->holders_.end() == p){
          return false;
        }
        
        return p.second->get_value(reinterpret_cast<const void **>(result));
      }      
    };
    
    class iscope_properties
    {
    public:
      iscope_properties(scope_properties_holder * owner)
      : holder_(owner)
      {
      }
      
      template <typename property_type>
      void add_property(const property_type & property)
      {
        this->holder_->add_property<property_type>(property);
      }
      
      template <typename property_type>
      void add_property(const std::function<void (property_type &)> & property_func)
      {
        this->holder_->add_property<property_type>(property_func);
      }
      
      template <typename property_type>
      bool get_property(const property_type ** result)
      {
        return this->holder_->get_property<property_type>(result);
      }
      
    private:
      std::shared_ptr<scope_properties_holder> holder_;
    };

    class scopped_service_provider : public iservice_provider_impl
    {
    public:
        scopped_service_provider(
          const std::shared_ptr<iservice_provider_impl> & parent);

        shutdown_event & get_shutdown_event() override;
    protected:
        iservice_factory * get_factory(size_t type) override;

    private:
        std::shared_ptr<iservice_provider_impl> parent_;
    };
    /////////////////////////////////////////////////////////////////////////////////
    class service_registrator_impl : public iservice_provider_impl
    {
    public:
        service_registrator_impl();
        ~service_registrator_impl();

        template <typename interface_type>
        void add_factory(const std::function<interface_type(const service_provider &, bool &)> & factory) {
            this->factory_[types::get_type_id<interface_type>()] = new service_factory<interface_type>(factory);
        }

        template <typename interface_type>
        void add_collection_factory(const std::function<interface_type(const service_provider &)> & factory) {
            service_collection_factory<interface_type> * collection;

            auto p = this->factory_.find(types::get_type_id<std::list<interface_type>>());
            if (p == this->factory_.end()) {
                collection = new service_collection_factory<interface_type>();
                this->factory_[types::get_type_id<std::list<interface_type>>()] = collection;
            }
            else {
                collection = static_cast<service_collection_factory<interface_type> *>(p->second);
            }

            collection->add(factory);
        }

        void add(iservice & service);
        void shutdown();

        shutdown_event & get_shutdown_event() {
            return this->shutdown_event_;
        }

        service_provider build(
          const foldername & current_user_folder,
          const foldername & local_machine_folder
        );

    protected:
        iservice_factory * get_factory(size_t type) override
        {
            auto p = this->factory_.find(type);
            if (p == this->factory_.end()) {
                return nullptr;
            }

            return p->second;
        }

    private:
        std::map<size_t, iservice_factory *> factory_;
        std::list<iservice *> services_;
        shutdown_event shutdown_event_;
    };

    template<typename interface_type>
    inline interface_type service_provider::get() const
    {
        return this->impl_->get<interface_type>(*this);
    }

    template<typename interface_type>
    inline bool service_provider::enum_collection(const std::function<bool(interface_type)>& visiter, bool throwIfEmpty) const
    {
        return this->impl_->enum_collection<interface_type>(*this, visiter, throwIfEmpty);
    }

    template<typename interface_type>
    inline std::list<interface_type> service_provider::get_collection(bool throwIfEmpty) const
    {
        return this->impl_->get_collection<interface_type>(*this, throwIfEmpty);
    }

    template<typename interface_type>
    inline void service_registrator::add_factory(
      const std::function<interface_type(const service_provider &, bool &)>& factory)
    {
        this->impl_->add_factory<interface_type>(factory);
    }
    
    template<typename interface_type>
    inline void service_registrator::add_collection_factory(const std::function<interface_type(const service_provider &)>& factory)
    {
        this->impl_->add_collection_factory<interface_type>(factory);
    }
    
    template<typename interface_type>
    inline interface_type iservice_provider_impl::get(const service_provider & sp)
    {
        std::lock_guard<std::recursive_mutex> lock(this->m_);

        auto type_id = types::get_type_id<interface_type>();
        auto p = this->scopped_objects_.find(type_id);
        if (this->scopped_objects_.end() != p) {
            return static_cast<object_holder<interface_type> *>(p->second)->value;
        }

        auto factory = this->get_factory(types::get_type_id<interface_type>());
        if (nullptr == factory) {
            throw new std::logic_error(
                string_format(
                    "interface %s not found",
                    typeid(interface_type).name()
                ));
        }

        bool is_scopped = false;
        auto result = static_cast<service_factory<interface_type> *>(factory)->get(sp, is_scopped);
        if (is_scopped) {
            this->scopped_objects_[type_id] = new object_holder<interface_type>(result);
        }

        return result;
    }
    template<typename interface_type>
    inline bool iservice_provider_impl::enum_collection(
      const service_provider & sp,
      const std::function<bool(interface_type)>& visiter,
      bool throwIfEmpty)
    {
        auto factory = this->get_factory(types::get_type_id<std::list<interface_type>>());
        if (nullptr == factory) {
            if (throwIfEmpty) {
                throw new std::logic_error(
                    string_format(
                        "collection of interface %s not found",
                        typeid(interface_type).name()
                    ));
            }

            return false;
        }
        else {
            return static_cast<service_collection_factory<interface_type> *>(factory)->enum_collection(sp, visiter);
        }
    }
    template<typename interface_type>
    inline std::list<interface_type> iservice_provider_impl::get_collection(
      const service_provider & sp,
      bool throwIfEmpty)
    {
        std::list<interface_type> result;
        this->enum_collection<interface_type>(
          sp,
          [&result](interface_type value) -> bool {
            result.push_back(value);
            return true;
        }, throwIfEmpty);

        return result;
    }

    template<typename interface_type>
    inline void iservice_provider_impl::service_collection_factory<interface_type>::add(
      const std::function<interface_type(const service_provider & sp)>& factory)
    {
        this->factories_.push_back(factory);
    }
    template<typename interface_type>
    inline bool iservice_provider_impl::service_collection_factory<interface_type>::enum_collection(
      const service_provider & sp, 
      const std::function<bool(interface_type)>& visiter)
    {
        for (auto p : this->factories_) {
            if (!visiter(p(sp))) {
                return false;
            }
        }

        return true;
    }
};

#endif // ! __VDS_CORE_SERVICE_PROVIDER_H_


