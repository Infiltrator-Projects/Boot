/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_COMMON_V2_PATH_H
#define BOOT_COMMON_V2_PATH_H
#include <stddef.h>
#include <stdint.h>
#define BOOT_V2_PATH_BYTES 4096u
/* Resolve an EFI UTF-16 pathname against an absolute V2 UTF-8 directory.
 * Normalize separators, dot and dot-dot without interpreting disk metadata.
 * Invalid UTF-16, invalid UTF-8, and overflowing paths fail closed. */
int boot_v2_path(const char *base,const uint16_t *name,char *out,size_t capacity);
int boot_v2_utf16(const char *name,size_t bytes,uint16_t *out,size_t capacity,size_t *units);
/* Consume one non-NUL UTF-8 scalar from an explicitly bounded buffer.
 * Reject overlong encodings, surrogate code points and values above
 * Unicode's maximum. Advance the cursor only after complete validation.
 * Kept inline so freestanding namespace and path tests remain independent. */
static inline int boot_v2_utf8_next(const char *text,size_t bytes,size_t *cursor,
                                    uint32_t *scalar)
{
    size_t i=*cursor;
    uint32_t c,min=0;
    unsigned extra=0;

    if(i>=bytes)return -1;
    c=(unsigned char)text[i++];
    if(!c)return -1;
    if(c<0x80){}
    else if(c>=0xc2&&c<=0xdf){c&=31;extra=1;min=0x80;}
    else if(c>=0xe0&&c<=0xef){c&=15;extra=2;min=0x800;}
    else if(c>=0xf0&&c<=0xf4){c&=7;extra=3;min=0x10000;}
    else return -1;
    if(extra>bytes-i)return -1;
    while(extra--){
        unsigned x=(unsigned char)text[i++];
        if((x&0xc0)!=0x80)return -1;
        c=(c<<6)|(x&63);
    }
    if(c<min||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return -1;
    *cursor=i;
    *scalar=c;
    return 0;
}
#endif
