#ifndef __VDS_CRYPTO_ASYMMETRICCRYPTO_H_
#define __VDS_CRYPTO_ASYMMETRICCRYPTO_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include <utility>

#include "hash.h"
#include "filename.h"
#include "crypto_service.h"
#include "stream.h"
#include "binary_serialize.h"
#include "const_data_buffer.h"

namespace vds {
  class _asymmetric_sign;
  class _asymmetric_sign_verify;
  class _asymmetric_public_key;
  class _certificate;
  class _ssl_tunnel;
  class _certificate_store;
  struct hash_info;
  
  struct asymmetric_crypto_info
  {
    int id;
    int key_bits;
  };
  
  class asymmetric_crypto
  {
  public:
    static const asymmetric_crypto_info & unknown();
    static const asymmetric_crypto_info & rsa2048();
    static const asymmetric_crypto_info & rsa4096();
  };
  
  class _asymmetric_private_key;
  class asymmetric_private_key
  {
  public:
    asymmetric_private_key();
    asymmetric_private_key(asymmetric_private_key && original);
    asymmetric_private_key(const asymmetric_crypto_info & info);
    ~asymmetric_private_key();
    
    static asymmetric_private_key generate(const asymmetric_crypto_info & info);

    static asymmetric_private_key parse(const std::string & value, const std::string & password = std::string());
    std::string str(const std::string & password = std::string()) const;
    
    const_data_buffer der(const std::string &password) const;
    static asymmetric_private_key parse_der(
      const const_data_buffer & value,
      const std::string & password);

    void load(const filename & filename, const std::string & password = std::string());
    void save(const filename & filename, const std::string & password = std::string()) const;

    const_data_buffer decrypt(const const_data_buffer & data) const;
    const_data_buffer decrypt(const void * data, size_t size) const;
    
    asymmetric_private_key & operator = (asymmetric_private_key && original);

  private:
    friend class _asymmetric_sign;
    friend class _asymmetric_private_key;
    friend class _asymmetric_public_key;
    friend class _certificate;
    friend class _ssl_tunnel;
    
    asymmetric_private_key(_asymmetric_private_key * impl);
    
    _asymmetric_private_key * impl_;
  };

  class _asymmetric_public_key;
  class asymmetric_public_key
  {
  public:
    asymmetric_public_key(const asymmetric_public_key & original);
    asymmetric_public_key(const asymmetric_private_key & key);
    ~asymmetric_public_key();

    static asymmetric_public_key parse(const std::string & format);
    std::string str() const;

    static asymmetric_public_key parse_der(const const_data_buffer & value);
    const_data_buffer der() const;

    void load(const filename & filename);
    void save(const filename & filename);

    const_data_buffer encrypt(const const_data_buffer & data);
    const_data_buffer encrypt(const void * data, size_t data_size);

  private:
    asymmetric_public_key(_asymmetric_public_key * impl);
    
    friend class _asymmetric_sign_verify;
    friend class _certificate;
    friend class _asymmetric_public_key;

    _asymmetric_public_key * impl_;
  };

  class asymmetric_sign : public stream_output_async<uint8_t>
  {
  public:
    asymmetric_sign(
      const hash_info & hash_info,
      const asymmetric_private_key & key);

    ~asymmetric_sign();

    const_data_buffer signature();

    static const_data_buffer signature(
      const hash_info & hash_info,
      const asymmetric_private_key & key,
      const const_data_buffer & data);

    static const_data_buffer signature(
      const hash_info & hash_info,
      const asymmetric_private_key & key,
      const void * data,
      size_t data_size);

    vds::async_task<void> write_async(
      
      const uint8_t  *data,
      size_t len) override;

  private:
    _asymmetric_sign * impl_;
  };

  class asymmetric_sign_verify : public stream_output_async<uint8_t>
  {
  public:
    asymmetric_sign_verify(
      const hash_info & hash_info,
      const asymmetric_public_key & key,
      const const_data_buffer & sig);

    ~asymmetric_sign_verify();
    
    bool result() const;
    
    static bool verify(
      const hash_info & hash_info,
      const asymmetric_public_key & key,
      const const_data_buffer & signature,
      const void * data,
      size_t data_size);

    static bool verify(
      const hash_info & hash_info,
      const asymmetric_public_key & key,
      const const_data_buffer & signature,
      const const_data_buffer & data);

    vds::async_task<void> write_async(      
      const uint8_t *data,
      size_t len) override;

  private:
    _asymmetric_sign_verify * impl_;
  };
  
  
  //http://www.codepool.biz/how-to-use-openssl-to-sign-certificate.html
  //openssl genrsa -out cakey.pem 2048
  //openssl req -new -days 365 -x509 -key cakey.pem -out cacert.crt
  //openssl rsa -in cakey.pem -pubout -out ca_pub.key
  //
  // openssl genrsa -out user.key 2048
  // openssl req -new -key user.key -out user.csr
  // openssl x509 -req -days 730 -in user.csr -CA cacert.crt -CAkey cakey.pem -CAcreateserial -out user.crt
  
  struct certificate_extension
  {
    certificate_extension()
      : oid(0)
    {
    }

    certificate_extension(
      crypto_service::certificate_extension_type extension_type,
      std::string && val)
    : oid(extension_type), value(std::move(val))
    {
    }

    crypto_service::certificate_extension_type oid;
    std::string value;
  };

  class _certificate;
  class certificate
  {
  public:
    certificate();
    certificate(const certificate & original) = delete;
    certificate(certificate && original);
    ~certificate();

    static certificate parse(const std::string & format);
    std::string str() const;

    static certificate parse_der(const const_data_buffer & body);
    const_data_buffer der() const;

    void load(const filename & filename);
    void save(const filename & filename) const;

    std::string subject() const;
    std::string issuer() const;
    const_data_buffer fingerprint(const hash_info & hash_algo = hash::sha256()) const;

    class create_options
    {
    public:
      create_options()
        : ca_certificate(nullptr),
          ca_certificate_private_key(nullptr)
      {
      }

      std::string country;
      std::string organization;
      std::string name;

      const certificate * ca_certificate;
      const asymmetric_private_key * ca_certificate_private_key;

      std::list<certificate_extension> extensions;
    };

    static certificate create_new(
      const asymmetric_public_key & new_certificate_public_key,
      const asymmetric_private_key & new_certificate_private_key,
      const create_options & options
    );

    asymmetric_public_key public_key() const;
    
    bool is_ca_cert() const;
    
    bool is_issued(const certificate & issuer) const;
    
    int extension_count() const;
    int extension_by_NID(int nid) const;
    certificate_extension get_extension(int index) const;

    certificate & operator = (const certificate & original) = delete;
    certificate & operator = (certificate && original);

  private:
    friend class _certificate;
    friend class _ssl_tunnel;
    friend class _certificate_store;
    
    certificate(_certificate * impl);
    
    _certificate * impl_;
  };

  class _certificate_store;
  class certificate_store
  {
  public:
    certificate_store();
    ~certificate_store();

    void add(const certificate & cert);
    void load_locations(const std::string & location);
    
    struct verify_result
    {
      int error_code;
      std::string error;
      std::string issuer;
    };
    
    verify_result verify(const certificate & cert) const;
    
  private:
    _certificate_store * impl_;
  };

inline vds::binary_serializer & operator << (vds::binary_serializer & s, const std::shared_ptr<vds::certificate> & cert)
{
	return s << cert->der();
}

inline vds::binary_deserializer & operator >> (vds::binary_deserializer & s, std::shared_ptr<vds::certificate> & cert)
{
	vds::const_data_buffer cert_data;
	s >> cert_data;
	cert = std::make_shared<vds::certificate>(vds::certificate::parse_der(cert_data));
	return s;
}

inline vds::binary_serializer & operator << (vds::binary_serializer & s, const std::shared_ptr<vds::asymmetric_private_key> & key)
{
  return s << key->der(std::string());
}

inline vds::binary_deserializer & operator >> (vds::binary_deserializer & s, std::shared_ptr<vds::asymmetric_private_key> & key)
{
  vds::const_data_buffer key_data;
  s >> key_data;
  key = std::make_shared<vds::asymmetric_private_key>(vds::asymmetric_private_key::parse_der(key_data, std::string()));
  return s;
}

}


#endif // __VDS_CRYPTO_ASYMMETRICCRYPTO_H_
