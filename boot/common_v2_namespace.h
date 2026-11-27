/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_COMMON_V2_NAMESPACE_H
#define BOOT_COMMON_V2_NAMESPACE_H
#include <infiltratr/filesystem_module_v2.h>
#include "common_v2_path.h"
/* Resolve only within the supplied mounted volume. Providers that already
 * follow links through CORE retain that behavior. When CORE exposes links
 * and METADATA.READLINK exists, resolve them without format-specific code.
 * The output path is absolute UTF-8 and is valid only on IFS_FS_OK. */
int boot_v2_resolve(const IfsFsCoreOpsV2 *,const IfsFsMetadataOpsV2 *,void *,
                   const char *,char [BOOT_V2_PATH_BYTES],IfsFsEntryV2 *,char [BOOT_V2_PATH_BYTES]);
int boot_v2_stat_entry(const IfsFsCoreOpsV2 *,void *,const char *,
                       IfsFsEntryV2 *,char [BOOT_V2_PATH_BYTES]);
#endif
