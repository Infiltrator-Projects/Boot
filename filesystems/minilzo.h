 




 









































 






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

