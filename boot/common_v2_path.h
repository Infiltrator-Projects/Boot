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
#endif
