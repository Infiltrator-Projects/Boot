/*
 * Bundled LZO configuration and compile-time ABI definitions used by the
 * filesystem decompression path. Word width, alignment and CPU endianness
 * must agree with the firmware build, not the development host alone.
 */

#ifndef __LZOCONF_H_INCLUDED
#define __LZOCONF_H_INCLUDED 1

#define LZO_VERSION             0x2050
#define LZO_VERSION_STRING      "2.05"
#define LZO_VERSION_DATE        "Apr 23 2011"

#if defined(LZO_HAVE_CONFIG_H)
#  include <config.h>
#endif
#include <limits.h>
#include <stddef.h>

/*
 * The compressed format and decoder pointer arithmetic count eight-bit
 * octets. Fail the build if a C byte on the target cannot represent that
 * unit.
 */
#if !defined(CHAR_BIT) || (CHAR_BIT != 8)
#  error "invalid CHAR_BIT"
#endif
#if !defined(UCHAR_MAX) || !defined(UINT_MAX) || !defined(ULONG_MAX)
#  error "check your compiler installation"
#endif
#if (USHRT_MAX < 1) || (UINT_MAX < 1) || (ULONG_MAX < 1)
#  error "your limits.h macros are broken"
#endif

#ifndef __LZODEFS_H_INCLUDED
#include "lzodefs.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(LZO_UINT32_C)
#  if (UINT_MAX < LZO_0xffffffffL)
#    define LZO_UINT32_C(c)     c ## UL
#  else
#    define LZO_UINT32_C(c)     ((c) + 0U)
#  endif
#endif

#if !defined(__LZO_CHECKER)
#  if defined(__BOUNDS_CHECKING_ON)
#    define __LZO_CHECKER       1
#  elif defined(__CHECKER__)
#    define __LZO_CHECKER       1
#  elif defined(__INSURE__)
#    define __LZO_CHECKER       1
#  elif defined(__PURIFY__)
#    define __LZO_CHECKER       1
#  endif
#endif

#if !defined(LZO_UINT_MAX)
#  if defined(LZO_ABI_LLP64)  
#    if defined(LZO_OS_WIN64)
     typedef unsigned __int64   lzo_uint;
     typedef __int64            lzo_int;
#    else
     typedef unsigned long long lzo_uint;
     typedef long long          lzo_int;
#    endif
#    define LZO_UINT_MAX        0xffffffffffffffffull
#    define LZO_INT_MAX         9223372036854775807LL
#    define LZO_INT_MIN         (-1LL - LZO_INT_MAX)
#  elif defined(LZO_ABI_IP32L64)  
     typedef unsigned int       lzo_uint;
     typedef int                lzo_int;
#    define LZO_UINT_MAX        UINT_MAX
#    define LZO_INT_MAX         INT_MAX
#    define LZO_INT_MIN         INT_MIN
#  elif (ULONG_MAX >= LZO_0xffffffffL)
     typedef unsigned long      lzo_uint;
     typedef long               lzo_int;
#    define LZO_UINT_MAX        ULONG_MAX
#    define LZO_INT_MAX         LONG_MAX
#    define LZO_INT_MIN         LONG_MIN
#  else
#    error "lzo_uint"
#  endif
#endif

#if !defined(LZO_UINT32_MAX)
#  if (UINT_MAX >= LZO_0xffffffffL)
     typedef unsigned int       lzo_uint32;
     typedef int                lzo_int32;
#    define LZO_UINT32_MAX      UINT_MAX
#    define LZO_INT32_MAX       INT_MAX
#    define LZO_INT32_MIN       INT_MIN
#  elif (ULONG_MAX >= LZO_0xffffffffL)
     typedef unsigned long      lzo_uint32;
     typedef long               lzo_int32;
#    define LZO_UINT32_MAX      ULONG_MAX
#    define LZO_INT32_MAX       LONG_MAX
#    define LZO_INT32_MIN       LONG_MIN
#  else
#    error "lzo_uint32"
#  endif
#endif

#if !defined(LZO_UINT64_MAX)
#  if (LZO_UINT_MAX >= LZO_0xffffffffL)
#   if ((((LZO_UINT_MAX) >> 31) >> 31) == 3)
#    define lzo_uint64          lzo_uint
#    define lzo_int64           lzo_int
#    define LZO_UINT64_MAX      LZO_UINT_MAX
#    define LZO_INT64_MAX       LZO_INT_MAX
#    define LZO_INT64_MIN       LZO_INT_MIN
#   endif
#  elif (ULONG_MAX >= LZO_0xffffffffL)
#   if ((((ULONG_MAX) >> 31) >> 31) == 3)
     typedef unsigned long      lzo_uint64;
     typedef long               lzo_int64;
#    define LZO_UINT64_MAX      ULONG_MAX
#    define LZO_INT64_MAX       LONG_MAX
#    define LZO_INT64_MIN       LONG_MIN
#   endif
#  endif
#endif

#if (LZO_UINT_MAX >= LZO_UINT32_MAX)
#  define lzo_xint              lzo_uint
#else
#  define lzo_xint              lzo_uint32
#endif

#if !defined(__LZO_MMODEL)
#  if (LZO_UINT_MAX <= UINT_MAX)
#    define __LZO_MMODEL         
#  elif defined(LZO_HAVE_MM_HUGE_PTR)
#    define __LZO_MMODEL_HUGE   1
#    define __LZO_MMODEL        __huge
#  else
#    define __LZO_MMODEL         
#  endif
#endif

#define lzo_bytep               unsigned char __LZO_MMODEL *
#define lzo_charp               char __LZO_MMODEL *
#define lzo_voidp               void __LZO_MMODEL *
#define lzo_shortp              short __LZO_MMODEL *
#define lzo_ushortp             unsigned short __LZO_MMODEL *
#define lzo_uint32p             lzo_uint32 __LZO_MMODEL *
#define lzo_int32p              lzo_int32 __LZO_MMODEL *
#if defined(LZO_UINT64_MAX)
#define lzo_uint64p             lzo_uint64 __LZO_MMODEL *
#define lzo_int64p              lzo_int64 __LZO_MMODEL *
#endif
#define lzo_uintp               lzo_uint __LZO_MMODEL *
#define lzo_intp                lzo_int __LZO_MMODEL *
#define lzo_xintp               lzo_xint __LZO_MMODEL *
#define lzo_voidpp              lzo_voidp __LZO_MMODEL *
#define lzo_bytepp              lzo_bytep __LZO_MMODEL *

#define lzo_byte                unsigned char __LZO_MMODEL

typedef int lzo_bool;

#if !defined(__LZO_EXTERN_C)
#  ifdef __cplusplus
#    define __LZO_EXTERN_C      extern "C"
#  else
#    define __LZO_EXTERN_C      extern
#  endif
#endif

#if !defined(__LZO_CDECL)
#  define __LZO_CDECL           __lzo_cdecl
#endif

#if !defined(__LZO_EXPORT1)
#  define __LZO_EXPORT1          
#endif
#if !defined(__LZO_EXPORT2)
#  define __LZO_EXPORT2          
#endif

#if !defined(LZO_PUBLIC)
#  define LZO_PUBLIC(_rettype)  __LZO_EXPORT1 _rettype __LZO_EXPORT2 __LZO_CDECL
#endif
#if !defined(LZO_EXTERN)
#  define LZO_EXTERN(_rettype)  __LZO_EXTERN_C LZO_PUBLIC(_rettype)
#endif
#if !defined(LZO_PRIVATE)
#  define LZO_PRIVATE(_rettype) static _rettype __LZO_CDECL
#endif

typedef int
(__LZO_CDECL *lzo_compress_t)   ( const lzo_bytep src, lzo_uint  src_len,
                                        lzo_bytep dst, lzo_uintp dst_len,
                                        lzo_voidp wrkmem );

typedef int
(__LZO_CDECL *lzo_decompress_t) ( const lzo_bytep src, lzo_uint  src_len,
                                        lzo_bytep dst, lzo_uintp dst_len,
                                        lzo_voidp wrkmem );

typedef int
(__LZO_CDECL *lzo_optimize_t)   (       lzo_bytep src, lzo_uint  src_len,
                                        lzo_bytep dst, lzo_uintp dst_len,
                                        lzo_voidp wrkmem );

typedef int
(__LZO_CDECL *lzo_compress_dict_t)(const lzo_bytep src, lzo_uint  src_len,
                                         lzo_bytep dst, lzo_uintp dst_len,
                                         lzo_voidp wrkmem,
                                   const lzo_bytep dict, lzo_uint dict_len );

typedef int
(__LZO_CDECL *lzo_decompress_dict_t)(const lzo_bytep src, lzo_uint  src_len,
                                           lzo_bytep dst, lzo_uintp dst_len,
                                           lzo_voidp wrkmem,
                                     const lzo_bytep dict, lzo_uint dict_len );

struct lzo_callback_t;
typedef struct lzo_callback_t lzo_callback_t;
#define lzo_callback_p lzo_callback_t __LZO_MMODEL *

typedef lzo_voidp (__LZO_CDECL *lzo_alloc_func_t)
    (lzo_callback_p self, lzo_uint items, lzo_uint size);
typedef void      (__LZO_CDECL *lzo_free_func_t)
    (lzo_callback_p self, lzo_voidp ptr);

typedef void (__LZO_CDECL *lzo_progress_func_t)
    (lzo_callback_p, lzo_uint, lzo_uint, int);

struct lzo_callback_t
{

    lzo_alloc_func_t nalloc;                 
    lzo_free_func_t nfree;                   

    lzo_progress_func_t nprogress;

    lzo_voidp user1;
    lzo_xint user2;
    lzo_xint user3;
};

#define LZO_E_OK                    0
#define LZO_E_ERROR                 (-1)
#define LZO_E_OUT_OF_MEMORY         (-2)     
#define LZO_E_NOT_COMPRESSIBLE      (-3)     
#define LZO_E_INPUT_OVERRUN         (-4)
#define LZO_E_OUTPUT_OVERRUN        (-5)
#define LZO_E_LOOKBEHIND_OVERRUN    (-6)
#define LZO_E_EOF_NOT_FOUND         (-7)
#define LZO_E_INPUT_NOT_CONSUMED    (-8)
#define LZO_E_NOT_YET_IMPLEMENTED   (-9)     
#define LZO_E_INVALID_ARGUMENT      (-10)

#ifndef lzo_sizeof_dict_t
#  define lzo_sizeof_dict_t     ((unsigned)sizeof(lzo_bytep))
#endif

#define lzo_init() __lzo_init_v2(LZO_VERSION,(int)sizeof(short),(int)sizeof(int),\
    (int)sizeof(long),(int)sizeof(lzo_uint32),(int)sizeof(lzo_uint),\
    (int)lzo_sizeof_dict_t,(int)sizeof(char *),(int)sizeof(lzo_voidp),\
    (int)sizeof(lzo_callback_t))
LZO_EXTERN(int) __lzo_init_v2(unsigned,int,int,int,int,int,int,int,int,int);

LZO_EXTERN(unsigned) lzo_version(void);
LZO_EXTERN(const char *) lzo_version_string(void);
LZO_EXTERN(const char *) lzo_version_date(void);
LZO_EXTERN(const lzo_charp) _lzo_version_string(void);
LZO_EXTERN(const lzo_charp) _lzo_version_date(void);

LZO_EXTERN(int)
    lzo_memcmp(const lzo_voidp a, const lzo_voidp b, lzo_uint len);
LZO_EXTERN(lzo_voidp)
    lzo_memcpy(lzo_voidp dst, const lzo_voidp src, lzo_uint len);
LZO_EXTERN(lzo_voidp)
    lzo_memmove(lzo_voidp dst, const lzo_voidp src, lzo_uint len);
LZO_EXTERN(lzo_voidp)
    lzo_memset(lzo_voidp buf, int c, lzo_uint len);

LZO_EXTERN(lzo_uint32)
    lzo_adler32(lzo_uint32 c, const lzo_bytep buf, lzo_uint len);
LZO_EXTERN(lzo_uint32)
    lzo_crc32(lzo_uint32 c, const lzo_bytep buf, lzo_uint len);
LZO_EXTERN(const lzo_uint32p)
    lzo_get_crc32_table(void);

LZO_EXTERN(int) _lzo_config_check(void);
typedef union { lzo_bytep p; lzo_uint u; } __lzo_pu_u;
typedef union { lzo_bytep p; lzo_uint32 u32; } __lzo_pu32_u;
typedef union { void *vp; lzo_bytep bp; lzo_uint u; lzo_uint32 u32; unsigned long l; } lzo_align_t;

LZO_EXTERN(unsigned) __lzo_align_gap(const lzo_voidp p, lzo_uint size);
#define LZO_PTR_ALIGN_UP(p,size) \
    ((p) + (lzo_uint) __lzo_align_gap((const lzo_voidp)(p),(lzo_uint)(size)))

#if defined(LZO_CFG_COMPAT)

#define __LZOCONF_H 1

#if defined(LZO_ARCH_I086)
#  define __LZO_i386 1
#elif defined(LZO_ARCH_I386)
#  define __LZO_i386 1
#endif

#if defined(LZO_OS_DOS16)
#  define __LZO_DOS 1
#  define __LZO_DOS16 1
#elif defined(LZO_OS_DOS32)
#  define __LZO_DOS 1
#elif defined(LZO_OS_WIN16)
#  define __LZO_WIN 1
#  define __LZO_WIN16 1
#elif defined(LZO_OS_WIN32)
#  define __LZO_WIN 1
#endif

#define __LZO_CMODEL             
#define __LZO_DMODEL             
#define __LZO_ENTRY             __LZO_CDECL
#define LZO_EXTERN_CDECL        LZO_EXTERN
#define LZO_ALIGN               LZO_PTR_ALIGN_UP

#define lzo_compress_asm_t      lzo_compress_t
#define lzo_decompress_asm_t    lzo_decompress_t

#endif  

#ifdef __cplusplus
}  
#endif

#endif  
