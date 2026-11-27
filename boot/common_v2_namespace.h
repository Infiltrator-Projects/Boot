/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_COMMON_V2_NAMESPACE_H
#define BOOT_COMMON_V2_NAMESPACE_H
#include <infiltratr/filesystem_module_v2.h>
#include <stddef.h>
#include <stdint.h>
#define BOOT_V2_PATH_BYTES 4096u
/* Translate EFI UTF-16 paths to normalized absolute UTF-8 paths. */
int boot_v2_path(const char *,const uint16_t *,char *,size_t);
int boot_v2_utf16(const char *,size_t,uint16_t *,size_t,size_t *);
/* Advance the cursor only after validating one complete non-NUL scalar. */
int boot_v2_utf8_next(const char *,size_t,size_t *,uint32_t *);
/* Resolve only within the supplied mounted volume. Providers that already
 * follow links through CORE retain that behavior. When CORE exposes links
 * and METADATA.READLINK exists, resolve them without format-specific code.
 * The output path is absolute UTF-8 and is valid only on IFS_FS_OK. */
int boot_v2_resolve(const IfsFsCoreOpsV2 *,const IfsFsMetadataOpsV2 *,void *,
                   const char *,char [BOOT_V2_PATH_BYTES],IfsFsEntryV2 *,char [BOOT_V2_PATH_BYTES]);
/* Validate a returned descriptor against the fixed host name buffer. */
int boot_v2_entry_valid(const IfsFsEntryV2 *,const char *,int directory);
int boot_v2_stat_entry(const IfsFsCoreOpsV2 *,void *,const char *,
                       IfsFsEntryV2 *,char [BOOT_V2_PATH_BYTES]);
#endif
