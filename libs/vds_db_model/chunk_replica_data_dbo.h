#ifndef __VDS_DB_MODEL_CHUNK_REPLICA_DATA_DBO_H_
#define __VDS_DB_MODEL_CHUNK_REPLICA_DATA_DBO_H_
/*
Copyright (c) 2017, Vadim Malyshev, lboss75@gmail.com
All rights reserved
*/

#include "database_orm.h"

namespace vds {
	namespace orm {
		class chunk_replica_data_dbo : public database_table {
		public:
			chunk_replica_data_dbo()
				: database_table("chunk_replica_data"),
				object_id(this, "object_id"),
				replica(this, "replica"),
        replica_hash(this, "replica_hash")
			{
			}

			database_column<const_data_buffer, std::string> object_id;
			database_column<int16_t, int> replica;
      database_column<const_data_buffer, std::string> replica_hash;
		};
	}
}

#endif //__VDS_DB_MODEL_CHUNK_REPLICA_DATA_DBO_H_
