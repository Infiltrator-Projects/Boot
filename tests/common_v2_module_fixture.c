/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Descriptor-only fixture. This does not implement or claim a filesystem. */
#include <infiltratr/filesystem_module_v2.h>
#ifndef BAD
#define BAD 0
#endif
static int probe(const IfsFsHostRuntimeV2 *host,uint32_t *variant)
{
    (void)host; (void)variant; return IFS_FS_UNSUPPORTED;
}
static const IfsFsCoreOpsV2 core={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),IFS_FS_GROUP_CORE,0,
        BAD==5?IFS_FS_CORE_PROBE|IFS_FS_CORE_READ:IFS_FS_CORE_PROBE},
    .probe=BAD==4?(int (*)(const IfsFsHostRuntimeV2 *,uint32_t *))(uintptr_t)1:probe
};
static const IfsFsGroupHeaderV2 *query(uint32_t id,uint32_t min,uint32_t max)
{
    if(id!=IFS_FS_GROUP_CORE || min!=2 || max!=2) return NULL;
    if(BAD==3) return (const IfsFsGroupHeaderV2 *)(uintptr_t)1;
    return &core.header;
}
#if BAD == 6
static IfsFsModuleApiV2 api={
#else
static const IfsFsModuleApiV2 api={
#endif
    .abi_version=BAD==7?1:IFS_FS_COMMON_ABI_V2,
    .structure_bytes=sizeof(IfsFsModuleApiV2),
    .format_id=BAD==8?(const char *)(uintptr_t)1:"fixture",
    .runtime_profiles=BAD==2?IFS_FS_PROFILE_HOSTED:IFS_FS_PROFILE_FREESTANDING|IFS_FS_PROFILE_FIRMWARE,
    .group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE),
    .query_group=BAD==9?(IfsFsQueryGroupV2)(uintptr_t)1:query
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void)
{
    if(BAD==1) return (const IfsFsModuleApiV2 *)(uintptr_t)1;
    return &api;
}
