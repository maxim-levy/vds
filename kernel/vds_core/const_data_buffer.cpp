/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/
#include "stdafx.h"
#include "const_data_buffer.h"
#include "binary_serialize.h"

void vds::const_data_buffer::serialize(binary_serializer & s) const
{
  s << *this;
}
