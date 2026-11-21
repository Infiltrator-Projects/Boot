 




 































#ifndef _FSW_POSIX_BASE_H_
#define _FSW_POSIX_BASE_H_


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define FSW_LITTLE_ENDIAN (1)
 


 

typedef int8_t              fsw_s8;
typedef uint8_t             fsw_u8;
typedef int16_t             fsw_s16;
typedef uint16_t            fsw_u16;
typedef int32_t             fsw_s32;
typedef uint32_t            fsw_u32;
typedef int64_t             fsw_s64;
typedef uint64_t            fsw_u64;


 

#define fsw_alloc(size, ptrptr) (((*(ptrptr) = malloc(size)) == NULL) ? FSW_OUT_OF_MEMORY : FSW_SUCCESS)
#define fsw_free(ptr) free(ptr)

 

#define fsw_memzero(dest,size) memset(dest,0,size)
#define fsw_memcpy(dest,src,size) memcpy(dest,src,size)
#define fsw_memeq(p1,p2,size) (memcmp(p1,p2,size) == 0)

 

#define FSW_MSGSTR(s) s
#define FSW_MSGFUNC(str, ...) (fprintf(stderr, str, ##__VA_ARGS__))

 

#define FSW_U64_SHR(val,shiftbits) ((val) >> (shiftbits))
#define FSW_U64_DIV(val,divisor) ((val) / (divisor))
#define DEBUG(x)

#define RShiftU64(val, shift) ((val) >> (shift))
#define LShiftU64(val, shift) ((val) << (shift))

#endif
