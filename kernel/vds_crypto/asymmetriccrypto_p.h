#ifndef __VDS_CRYPTO_ASYMMETRICCRYPTO_P_H_
#define __VDS_CRYPTO_ASYMMETRICCRYPTO_P_H_

/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "asymmetric_private_key.h"

namespace vds {

  class _asymmetric_private_key
  {
  public:
    _asymmetric_private_key();
    _asymmetric_private_key(EVP_PKEY * key);
    _asymmetric_private_key(const asymmetric_crypto_info & info);
    ~_asymmetric_private_key();
    
    void generate();

    static asymmetric_private_key parse(const std::string & value, const std::string & password = std::string());
    std::string str(const std::string & password = std::string()) const;

    void load(const filename & filename, const std::string & password = std::string());
    void save(const filename & filename, const std::string & password = std::string()) const;

    EVP_PKEY * key() const
    {
      return this->key_;
    }

    data_buffer decrypt(const data_buffer & data);

  private:
    friend class asymmetric_sign;
    friend class asymmetric_public_key;

    const asymmetric_crypto_info & info_;
    EVP_PKEY_CTX * ctx_;
    EVP_PKEY * key_;
  };

  class _asymmetric_public_key
  {
  public:
    _asymmetric_public_key(EVP_PKEY * key);
    _asymmetric_public_key(asymmetric_public_key && original);
    _asymmetric_public_key(const asymmetric_private_key & key);
    ~_asymmetric_public_key();

    EVP_PKEY * key() const
    {
      return this->key_;
    }

    static asymmetric_public_key parse(const std::string & format);
    std::string str() const;

    void load(const filename & filename);
    void save(const filename & filename);

    data_buffer encrypt(const data_buffer & data);

  private:
    friend class asymmetric_sign_verify;
    const asymmetric_crypto_info & info_;
    EVP_PKEY * key_;
  };

  class _asymmetric_sign
  {
  public:
    asymmetric_sign(
      const hash_info & hash_info,
      const asymmetric_private_key & key
    );
    ~asymmetric_sign();

    void update(
      const void * data,
      int len);

    void final();

    const data_buffer & signature() const {
      return this->sig_;
    }

  private:
    EVP_MD_CTX * ctx_;
    const EVP_MD * md_;
    data_buffer sig_;
  };

  class _asymmetric_sign_verify
  {
  public:
    _asymmetric_sign_verify(
      const hash_info & hash_info,
      const asymmetric_public_key & key
    );
    ~_asymmetric_sign_verify();

    void update(
      const void * data,
      int len);

    bool verify(
      const data_buffer & sig
    );

  private:
    EVP_MD_CTX * ctx_;
    const EVP_MD * md_;
  };
  
  
  //http://www.codepool.biz/how-to-use-openssl-to-sign-certificate.html
  //openssl genrsa -out cakey.pem 2048
  //openssl req -new -days 365 -x509 -key cakey.pem -out cacert.crt
  //openssl rsa -in cakey.pem -pubout -out ca_pub.key
  //
  // openssl genrsa -out user.key 2048
  // openssl req -new -key user.key -out user.csr
  // openssl x509 -req -days 730 -in user.csr -CA cacert.crt -CAkey cakey.pem -CAcreateserial -out user.crt
  
  class _certificate
  {
  public:
    _certificate();
    _certificate(certificate && original);
    _certificate(X509 * cert);
    ~_certificate();

    static certificate parse(const std::string & format);
    std::string str() const;

    void load(const filename & filename);
    void save(const filename & filename) const;

    X509 * cert() const
    {
      return this->cert_;
    }

    std::string subject() const;
    std::string issuer() const;
    data_buffer fingerprint(const hash_info & hash_algo = hash::sha256()) const;

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

  private:
    X509 * cert_;

    static bool add_ext(X509 * cert, int nid, const char *value);
  };
  
  class _certificate_store
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
    X509_STORE * store_;
  };
}

#endif // __VDS_CRYPTO_ASYMMETRICCRYPTO_P_H_
