/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "stdafx.h"
#include "upnp_client.h"

vds::upnp_client::upnp_client(const vds::service_provider& sp)
: log_(sp, "UPNP client")
#ifdef _WIN32
  , nat_(nullptr)
  , port_mappings_(nullptr)
#else
  , devlist_(nullptr)
#endif
{
#ifdef _WIN32
#else
  memset(&this->upnp_urls_, 0, sizeof(this->upnp_urls_));
#endif
}

vds::upnp_client::~upnp_client()
{
#ifdef _WIN32
#else
  FreeUPNPUrls(&this->upnp_urls_);
  
  if(nullptr != this->devlist_){
    freeUPNPDevlist(this->devlist_);
  }
#endif
}


bool vds::upnp_client::open_port(
  uint16_t internal_port,
  uint16_t external_port,
  const std::string & protocol,
  const std::string & description
)
{
  this->log_(ll_debug, "Execute the UPNP discovery process");

#ifdef _WIN32
  void * nat;
  auto hr = CoCreateInstance(
    CLSID_UPnPNAT,
    NULL,
    CLSCTX_INPROC_SERVER,
    IID_IUPnPNAT,
    &nat);
  if(FAILED(hr)) {
    this->log_(ll_error, "Failed to create instance of IUPnPNAT. Error: %d", hr);
    throw new std::system_error(hr, std::system_category(), "Failed to create instance of IUPnPNAT");
  }
  this->nat_.reset((IUPnPNAT *)nat);

  for (int i = 0; i < 10; ++i) {
    IStaticPortMappingCollection * port_mappings = nullptr;
    hr = this->nat_->get_StaticPortMappingCollection(&port_mappings);
    if (SUCCEEDED(hr) && nullptr != port_mappings) {
      this->port_mappings_.reset(port_mappings);
      break;
    }

    Sleep(1000);
  }

  this->nat_.release();

  if (!this->port_mappings_) {
    return false;
  }
  
  dns_address_info addr(dns::hostname());
  
  if (this->port_mappings_) {
    IStaticPortMapping * port_mapping = nullptr;
    hr = this->port_mappings_->Add(
      external_port,
      bstr_ptr(SysAllocString((const OLECHAR *)utf16::from_utf8(protocol).c_str())).get(),
      internal_port,
      bstr_ptr(SysAllocString((const OLECHAR *)utf16::from_utf8(inet_ntoa(*(in_addr*)addr.first()->ai_addr)).c_str())).get(),
      VARIANT_TRUE,
      bstr_ptr(SysAllocString((const OLECHAR *)utf16::from_utf8(description).c_str())).get(),
      &port_mapping);

    if (SUCCEEDED(hr)) {
      port_mapping->Release();
      return true;
    }
  }

  this->log_(ll_error, "Failed to add port mapping. Error code: %d", hr);
  return false;

#else

  int error_code;
  this->devlist_ = upnpDiscover(2000, NULL, NULL, 0, 0, &error_code);
  if(nullptr == this->devlist_) {
    this->log_(ll_error, "No UPnP device found on the network. Error: %d", error_code);
  }
  else {
    auto status = UPNP_GetValidIGD(
      this->devlist_,
      &this->upnp_urls_,
      &this->igd_data_,
      this->lanaddr_,
      sizeof(this->lanaddr_));
    if(0 == status){
      this->log_(ll_error, "No IGD found");
    }
    else {
      this->log_(ll_debug, "Found IGD %s (status %d)", this->upnp_urls_.controlURL, status);
      
      int r = UPNP_AddPortMapping(
				this->upnp_urls_.controlURL,
				this->igd_data_.first.servicetype,
				std::to_string(internal_port).c_str(),
				std::to_string(external_port).c_str(),
				this->lanaddr_,
				description.c_str(),
				protocol.c_str(),
				0,
				0);
      if (r == UPNPCOMMAND_SUCCESS) {
        this->log_(ll_debug, "Added mapping %s %d to %s:%d", protocol.c_str(), external_port, this->lanaddr_, internal_port);
        return true;
      }
      else {
        this->log_(ll_error, "open_port failed with code %d(%s)", r, strupnperror(r));
      }
    }
  }
  
  return false;
#endif
}

void vds::upnp_client::close_port(
  uint16_t external_port,
  const std::string& protocol)
{
#ifdef _WIN32
  auto hr = this->port_mappings_->Remove(
    external_port,
    bstr_ptr(SysAllocString((const OLECHAR *)utf16::from_utf8(protocol).c_str())).get()
  );

  if (FAILED(hr)) {
    throw new std::system_error(hr, std::system_category(), "Failed to remove port mapping");
  }
#else
  int r = UPNP_DeletePortMapping(
    this->upnp_urls_.controlURL,
    this->igd_data_.first.servicetype,
    std::to_string(external_port).c_str(),
    protocol.c_str(),
    0);
  if (r == UPNPCOMMAND_SUCCESS) {
    this->log_(ll_debug, "Removed mapping %s %d", protocol.c_str(), external_port);
  }
  else {
    this->log_(ll_error, "close_port failed with code %d(%s)", r, strupnperror(r));
  }
#endif
}
