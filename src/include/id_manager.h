// vim:noexpandtab:shiftwidth=8:tabstop=8:
#ifndef _ID_MANAGER_H
#define _ID_MANAGER_H

#define TXN_UUID_LEN 16

#ifdef __cplusplus
extern "C" {
#endif
#include "lwrapper.h"
#include <endian.h>

typedef struct uuid {
#if (BYTE_ORDER != BIG_ENDIAN)
	uint64_t lo;
	uint64_t hi;
#else
	uint64_t hi;
	uint64_t lo;
#endif
} uuid_t;

// Returns 0 upon success
int initialize_id_manager(const db_store_t* db);

// Returns an opaque 16 byte buffer containing the file ID
// This buffer may contain null bytes
char* generate_file_id(const db_store_t* db);

// Returns a file ID that represents the root of the file system.
char* get_root_id(const db_store_t* db);

// Returns the lower-64 bits of the id.
uint64_t get_lower_half(const char* id);
// Returns the lower-64 bits of the id.
uint64_t get_upper_half(const char* id);

// Returns the root |UUID|.
uuid_t uuid_root();

// Returns a special value that represents an invalid UUID.
uuid_t uuid_null();

// Returns the next UUID after |current|.
uuid_t uuid_next(uuid_t current);

// Pre-allocates |n| consecutive UUIDs and returns the value of the first one.
uuid_t uuid_allocate(const db_store_t* db, int n);

// Returns a buf that encodes the given |id|. The size of the buf is fixed to
// TXN_UUID_LEN.
char* uuid_to_buf(uuid_t id);

// |buf| should be allocated and memory size is at least |TXN_UUID_LEN|.
void uuid_serialize(uuid_t id, char* buf);

// Decodes uuid in |buf| whose size is fixed to TXN_UUID_LEN.
uuid_t buf_to_uuid(const char* buf);

inline bool uuid_is_null(uuid_t id) { return (id.lo == 0) && (id.hi == 0); }

#ifdef __cplusplus
}
#endif

// Returns a base10 null terminated ascii representation of
// the opaque file id returned by generate_file_id()
char* id_to_string(const char* buf);

#endif  // _ID_MANAGER_H
