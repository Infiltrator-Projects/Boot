/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common_v2_module.h"

/* AMD64 SysV descriptors contain native eight-byte function pointers.
 * These assertions intentionally fail compilation if Libraries changes
 * the group layout. Only canonical ABI types are used. */
#define CHECK_LAYOUT(type,first,count) \
    _Static_assert(offsetof(type,first)==sizeof(IfsFsGroupHeaderV2) && \
        sizeof(type)==sizeof(IfsFsGroupHeaderV2)+(count)*sizeof(uintptr_t), \
        "Unexpected Common V2 callback layout")
_Static_assert(sizeof(uintptr_t)==8 && sizeof(IfsFsModuleApiEntryV2)==8,
               "AMD64 firmware module profile only");
CHECK_LAYOUT(IfsFsCoreOpsV2,probe,11);
CHECK_LAYOUT(IfsFsHandlesV2,open,9);
CHECK_LAYOUT(IfsFsMetadataOpsV2,get,9);
CHECK_LAYOUT(IfsFsVolumeOpsV2,query,10);
CHECK_LAYOUT(IfsFsDefragOpsV2,map,6);
CHECK_LAYOUT(IfsFsStreamsOpsV2,list,4);
CHECK_LAYOUT(IfsFsSnapshotOpsV2,list,4);
CHECK_LAYOUT(IfsFsAllocationOpsV2,map,1);
CHECK_LAYOUT(IfsFsFormatProfileOpsV2,get_default,1);
#undef CHECK_LAYOUT

static void copy_bytes(void *dst,const void *src,size_t n)
{
    unsigned char *d=dst; const unsigned char *s=src;
    while(n--) *d++=*s++;
}
static int immutable(const BootV2ElfImage *i,uintptr_t p,size_t n)
{
    return boot_v2_elf_contains(i,p,n,BOOT_V2_ELF_R,BOOT_V2_ELF_W|BOOT_V2_ELF_X);
}
static int executable(const BootV2ElfImage *i,uintptr_t p)
{
    return boot_v2_elf_contains(i,p,1,BOOT_V2_ELF_R|BOOT_V2_ELF_X,BOOT_V2_ELF_W);
}
/* Check each character before dereferencing a possibly invalid pointer. */
static int format_matches(const BootV2ElfImage *i,const char *name,const char *expected)
{
    uintptr_t p=(uintptr_t)name;
    unsigned j;
    if(!ifs_fs_format_id_valid(expected) || !p) return 0;
    for(j=0;j<=IFS_FS_MAX_FORMAT_ID;++j) {
        if(p>UINTPTR_MAX-j || !immutable(i,p+j,1)) return 0;
        if(name[j]!=expected[j]) return 0;
        if(!name[j]) return j!=0;
    }
    return 0;
}
static int group_shape(uint32_t id,size_t *bytes,uint64_t *caps)
{
#define SHAPE(type,mask) do { *bytes=sizeof(type); *caps=(mask); return 1; } while(0)
    switch(id) {
    case IFS_FS_GROUP_CORE: SHAPE(IfsFsCoreOpsV2,IFS_FS_CORE_ALL);
    case IFS_FS_GROUP_HANDLES: SHAPE(IfsFsHandlesV2,IFS_FS_HANDLE_ALL);
    case IFS_FS_GROUP_METADATA: SHAPE(IfsFsMetadataOpsV2,IFS_FS_META_ALL);
    case IFS_FS_GROUP_VOLUME: SHAPE(IfsFsVolumeOpsV2,IFS_FS_VOLUME_ALL);
    case IFS_FS_GROUP_DEFRAG: SHAPE(IfsFsDefragOpsV2,IFS_FS_DEFRAG_ALL);
    case IFS_FS_GROUP_STREAMS: SHAPE(IfsFsStreamsOpsV2,IFS_FS_STREAM_ALL);
    case IFS_FS_GROUP_SNAPSHOTS: SHAPE(IfsFsSnapshotOpsV2,IFS_FS_SNAPSHOT_ALL);
    case IFS_FS_GROUP_ALLOCATION: SHAPE(IfsFsAllocationOpsV2,IFS_FS_ALLOCATION_ALL);
    case IFS_FS_GROUP_FORMAT_PROFILE: SHAPE(IfsFsFormatProfileOpsV2,IFS_FS_FORMAT_PROFILE_ALL);
    default: return 0;
    }
#undef SHAPE
}
static int group_valid(const BootV2ElfImage *i,const IfsFsGroupHeaderV2 *g,uint32_t id)
{
    size_t bytes,j; uint64_t caps;
    if(!group_shape(id,&bytes,&caps) ||
       (uintptr_t)g%_Alignof(IfsFsGroupHeaderV2) ||
       !immutable(i,(uintptr_t)g,sizeof(*g)) ||
       !ifs_fs_group_v2_header_valid(g,id,(uint32_t)bytes,caps) ||
       !immutable(i,(uintptr_t)g,g->structure_bytes)) return 0;
    /* All known callback slots must point into executable file-backed
     * segments of this exact authenticated image, or be NULL. */
    for(j=sizeof(*g);j<bytes;j+=sizeof(uintptr_t)) {
        uintptr_t fn=0;
        copy_bytes(&fn,(const unsigned char *)g+j,sizeof(fn));
        if(fn && !executable(i,fn)) return 0;
    }
    return ifs_fs_group_v2_callbacks_valid(g);
}
int boot_v2_module_inspect(const BootV2ElfImage *image,const char *expected,
                           BootV2ModuleView *out)
{
    BootV2ModuleView v={0};
    IfsFsModuleApiEntryV2 entry;
    const IfsFsModuleApiV2 *api;
    const IfsFsGroupHeaderV2 *core,*volume;
    unsigned id;
    const uint64_t required_profiles=IFS_FS_PROFILE_FREESTANDING|IFS_FS_PROFILE_FIRMWARE;
    const uint64_t mount_groups=IFS_FS_GROUP_BIT(IFS_FS_GROUP_HANDLES)|
        IFS_FS_GROUP_BIT(IFS_FS_GROUP_METADATA)|IFS_FS_GROUP_BIT(IFS_FS_GROUP_DEFRAG)|
        IFS_FS_GROUP_BIT(IFS_FS_GROUP_STREAMS)|IFS_FS_GROUP_BIT(IFS_FS_GROUP_SNAPSHOTS)|
        IFS_FS_GROUP_BIT(IFS_FS_GROUP_ALLOCATION);
    if(!out || out->image || out->api || !image ||
       !ifs_fs_format_id_valid(expected) ||
       !executable(image,(uintptr_t)image->entry)) return IFS_FS_INVALID;
    copy_bytes(&entry,&image->entry,sizeof(entry));
    api=entry();
    if((uintptr_t)api%_Alignof(IfsFsModuleApiV2) ||
       !immutable(image,(uintptr_t)api,sizeof(*api)) ||
       api->abi_version!=IFS_FS_COMMON_ABI_V2 || api->structure_bytes<sizeof(*api) ||
       !immutable(image,(uintptr_t)api,api->structure_bytes) ||
       (api->runtime_profiles&required_profiles)!=required_profiles ||
       (api->runtime_profiles&~IFS_FS_PROFILE_ALL) ||
       (api->group_mask&~IFS_FS_GROUP_ALL) ||
       !(api->group_mask&IFS_FS_GROUP_BIT(IFS_FS_GROUP_CORE)) ||
       !format_matches(image,api->format_id,expected) ||
       !executable(image,(uintptr_t)api->query_group)) return IFS_FS_INVALID;
    for(id=1;id<=IFS_FS_GROUP_MAX;++id) {
        if(!(api->group_mask&IFS_FS_GROUP_BIT(id))) continue;
        /* Query exactly once. The canonical callbacks validator operates
         * only after the returned table and every known slot are bounded. */
        v.groups[id]=api->query_group(id,IFS_FS_GROUP_ABI_V2,IFS_FS_GROUP_ABI_V2);
        if(!group_valid(image,v.groups[id],id)) return IFS_FS_INVALID;
    }
    core=v.groups[IFS_FS_GROUP_CORE]; volume=v.groups[IFS_FS_GROUP_VOLUME];
    if(!(core->capabilities&IFS_FS_CORE_PROBE) ||
       ((api->group_mask&mount_groups) && !(core->capabilities&IFS_FS_CORE_MOUNT)) ||
       (volume && (volume->capabilities&~IFS_FS_VOLUME_FORMAT) &&
        !(core->capabilities&IFS_FS_CORE_MOUNT)) ||
       (v.groups[IFS_FS_GROUP_FORMAT_PROFILE] &&
        (!volume || !(volume->capabilities&IFS_FS_VOLUME_FORMAT)))) return IFS_FS_INVALID;
    v.image=image; v.api=api; *out=v;
    return IFS_FS_OK;
}
const IfsFsCoreOpsV2 *boot_v2_module_readable_core(const BootV2ModuleView *v)
{
    const uint64_t needed=IFS_FS_CORE_PROBE|IFS_FS_CORE_MOUNT|IFS_FS_CORE_STAT|
        IFS_FS_CORE_LIST|IFS_FS_CORE_READ;
    const IfsFsGroupHeaderV2 *core;
    if(!v || !v->image || !v->image->entry || !v->api) return NULL;
    core=v->groups[IFS_FS_GROUP_CORE];
    return core && (core->capabilities&needed)==needed ? (const IfsFsCoreOpsV2 *)core : NULL;
}
