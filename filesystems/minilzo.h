/*
 * Public entry points and types for the bundled minimal LZO decoder.
 * Compressed blocks originate from untrusted filesystem extents; callers
 * must supply actual source/destination lengths and propagate failures.
 */

#ifndef __MINILZO_H
#define __MINILZO_H 1

#define MINILZO_VERSION         0x2050

#ifdef __LZOCONF_H
#  error "you cannot use both LZO and miniLZO"
#endif

#undef LZO_HAVE_CONFIG_H
#include "lzoconf.h"

#if !defined(LZO_VERSION) || (LZO_VERSION != MINILZO_VERSION)
#  error "version mismatch in header files"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define LZO1X_MEM_COMPRESS      LZO1X_1_MEM_COMPRESS
#define LZO1X_1_MEM_COMPRESS    ((lzo_uint32) (16384L * lzo_sizeof_dict_t))
/*
 * Decompression requires no caller-supplied scratch dictionary. LZO
 * back-references use the output already produced. The safe entry point also
 * takes the destination capacity through dst_len on entry.
 */
#define LZO1X_MEM_DECOMPRESS    (0)

LZO_EXTERN(int)
lzo1x_1_compress        ( const lzo_bytep src, lzo_uint  src_len,
                                lzo_bytep dst, lzo_uintp dst_len,
                                lzo_voidp wrkmem );

LZO_EXTERN(int)
lzo1x_decompress        ( const lzo_bytep src, lzo_uint  src_len,
                                lzo_bytep dst, lzo_uintp dst_len,
                                lzo_voidp wrkmem   );

LZO_EXTERN(int)
lzo1x_decompress_safe   ( const lzo_bytep src, lzo_uint  src_len,
                                lzo_bytep dst, lzo_uintp dst_len,
                                lzo_voidp wrkmem   );

#ifdef __cplusplus
}  
#endif

#endif  
