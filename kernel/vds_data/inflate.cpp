/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "stdafx.h"
#include "inflate.h"
#include "private/inflate_p.h"

vds::inflate::inflate(const std::shared_ptr<stream_output_async<uint8_t>> & target)
: impl_(new _inflate_handler(target))
{
}

vds::inflate::~inflate() {
  delete this->impl_;
}

vds::const_data_buffer vds::inflate::decompress(
  
  const void * data,
  size_t size)
{
	auto result = std::make_shared<collect_data<uint8_t>>();
  _inflate_handler inf(result);

	inf.write_async((const uint8_t *)data, size).get();
	inf.write_async(nullptr, 0).get();

	return result->move_data();
}

vds::async_task<void> vds::inflate::write_async( const uint8_t *data, size_t len) {
  return this->impl_->write_async(data, len);
}

