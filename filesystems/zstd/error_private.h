/*
 * Private Zstandard decompressor error-code representation. Success,
 * truncated input, corruption and resource failure must remain distinct
 * through internal helpers and the public filesystem read response.
 */
 















 

#ifndef ERROR_H_MODULE
#define ERROR_H_MODULE

 


#include "zstd.h"   

 


#define PREFIX(name) ZSTD_error_##name

 


#define ERROR(name) ((size_t)-PREFIX(name))

#define ERR_isError ZSTD_isError  
#define FSE_isError ZSTD_isError
#define HUF_isError ZSTD_isError
#endif  
