/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Negative qualification fixture only: deliberately overlaps an NTFS probe
 * and must NEVER reach mount. It is not a filesystem implementation and is
 * not shipped, built or installed by any production release. */
#include <infiltratr/filesystem_module_v2.h>
static int probe(const IfsFsHostRuntimeV2 *h,uint32_t *variant)
{
    uint8_t bytes[8]; const char ntfs[]="NTFS    ";
    int rc=ifs_fs_host_read_bytes_v2(h,3,bytes,sizeof(bytes));
    if(rc!=IFS_FS_OK)return rc;
    for(unsigned i=0;i<sizeof(bytes);i++)if(bytes[i]!=(uint8_t)ntfs[i])return IFS_FS_NOT_FOUND;
    *variant=1;return IFS_FS_OK;
}
static int mount(const IfsFsHostRuntimeV2 *h,void **v,IfsFsVolumeInfoV2 *i)
{(void)h;(void)v;(void)i;return IFS_FS_UNSUPPORTED;}
static int unmount(void *v){(void)v;return IFS_FS_UNSUPPORTED;}
static int stat(void *v,const char *p,IfsFsEntryV2 *e)
{(void)v;(void)p;(void)e;return IFS_FS_UNSUPPORTED;}
static int list(void *v,const char *p,uint32_t n,IfsFsEntryV2 *e)
{(void)v;(void)p;(void)n;(void)e;return IFS_FS_UNSUPPORTED;}
static int read(void *v,const char *p,uint64_t off,void *b,uint32_t n)
{(void)v;(void)p;(void)off;(void)b;(void)n;return IFS_FS_UNSUPPORTED;}
static const IfsFsCoreOpsV2 core={
    .header={IFS_FS_GROUP_ABI_V2,sizeof(IfsFsCoreOpsV2),IFS_FS_GROUP_CORE,0,
        IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|IFS_FS_CORE_STAT|IFS_FS_CORE_LIST|IFS_FS_CORE_READ},
    .probe=probe,.mount=mount,.unmount=unmount,.stat=stat,.directory_entry=list,.read=read
};
static const IfsFsGroupHeaderV2 *query(uint32_t id,uint32_t min,uint32_t max)
{return id==IFS_FS_GROUP_CORE&&min==2&&max==2?&core.header:NULL;}
static const IfsFsModuleApiV2 api={
    .abi_version=IFS_FS_COMMON_ABI_V2,.structure_bytes=sizeof(IfsFsModuleApiV2),
    .format_id="overlap",.runtime_profiles=IFS_FS_PROFILE_FREESTANDING|IFS_FS_PROFILE_FIRMWARE,
    .group_mask=IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE),.query_group=query
};
__attribute__((visibility("default")))
const IfsFsModuleApiV2 *infiltrator_filesystem_api_v2(void){return &api;}
