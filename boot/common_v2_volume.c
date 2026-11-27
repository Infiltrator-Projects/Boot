/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common_v2_volume.h"
#include "common_v2_discovery.h"
#include "common_v2_uefi_storage.h"
#include "common_v2_namespace.h"
#include "lib.h"
#include "log.h"
#include "../include/boot_call_wrapper.h"

/* Common modules use AMD64 SysV, whereas published EFI protocols must
 * use the firmware Microsoft ABI even when GNU-EFI's EFIAPI is empty. */
#define V2_EFI __attribute__((ms_abi))
#define CALLBACK(field,fn) ((__typeof__(field))(fn))
#define MAX_VOLUMES 128U
#define READ_CHUNK (1024U*1024U)
static EFI_GUID sfs_guid={0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_GUID file_guid=EFI_FILE_INFO_ID;
static EFI_GUID fs_guid=EFI_FILE_SYSTEM_INFO_ID;
static EFI_GUID label_guid=EFI_FILE_SYSTEM_VOLUME_LABEL_INFO_ID;
typedef struct V2Volume {
    EFI_FILE_IO_INTERFACE sfs; /* first member: EFI callback owner */
    struct V2Volume *next;
    BootV2UefiStorage storage;
    BootV2Provider *provider;
    const IfsFsCoreOpsV2 *core;
    void *mounted;
    IfsFsVolumeInfoV2 info;
    UINTN opens;
    BOOLEAN published;
} V2Volume;
typedef struct {
    EFI_FILE_PROTOCOL file; /* first member: EFI callback owner */
    V2Volume *volume;
    UINT64 size,position;
    uint32_t type,index;
    char path[BOOT_V2_PATH_BYTES];
} V2File;
static V2Volume *volumes;
static UINTN volume_count;
static EFI_STATUS result(int rc)
{
    switch(rc){
    case IFS_FS_OK:return EFI_SUCCESS;
    case IFS_FS_NOT_FOUND:return EFI_NOT_FOUND;
    case IFS_FS_INVALID:return EFI_INVALID_PARAMETER;
    case IFS_FS_NO_MEMORY:return EFI_OUT_OF_RESOURCES;
    case IFS_FS_NO_SPACE:return EFI_VOLUME_FULL;
    case IFS_FS_ACCESS:return EFI_ACCESS_DENIED;
    case IFS_FS_CORRUPT:return EFI_VOLUME_CORRUPTED;
    case IFS_FS_UNSUPPORTED:return EFI_UNSUPPORTED;
    case IFS_FS_BUSY:return EFI_NOT_READY;
    default:return EFI_DEVICE_ERROR;
    }
}
static BOOLEAN live(V2Volume *v)
{
    /* Zero-length read still validates current device identity; never
     * return cached directory/file metadata for changed media. */
    return v && v->mounted && !boot_v2_storage_read(&v->storage.storage,0,NULL,0);
}
static int valid_entry(const IfsFsEntryV2 *e,const char *name,size_t cap)
{
    return e->structure_bytes==sizeof(*e) && e->name==name &&
        e->name_capacity==cap && e->name_bytes<cap && name[e->name_bytes]==0 &&
        (e->type==IFS_FS_ENTRY_DIRECTORY||e->type==IFS_FS_ENTRY_FILE||e->type==IFS_FS_ENTRY_LINK);
}
static int stat(V2Volume *v,const char *path,IfsFsEntryV2 *e,char *name)
{
    return boot_v2_stat_entry(v->core,v->mounted,path,e,name);
}
static EFI_STATUS fill_file(UINTN *bytes,VOID *buffer,const IfsFsEntryV2 *entry,
                            const char *name,size_t n)
{
    uint16_t text[BOOT_V2_PATH_BYTES];size_t units;
    UINTN required;
    EFI_FILE_INFO *info=buffer;
    if(!bytes || boot_v2_utf16(name,n,text,BOOT_V2_PATH_BYTES,&units))return EFI_VOLUME_CORRUPTED;
    required=SIZE_OF_EFI_FILE_INFO+(units+1)*sizeof(CHAR16);
    if(*bytes<required){*bytes=required;return EFI_BUFFER_TOO_SMALL;}
    if(!buffer)return EFI_INVALID_PARAMETER;
    SetMem(buffer,required,0);
    info->Size=required;info->FileSize=entry->size_bytes;
    /* V2 CORE does not report allocated physical size or timestamps.
     * Leave these optional values unknown (zero), never infer allocation
     * from logical length. The protocol is always read-only. */
    info->Attribute=EFI_FILE_READ_ONLY|(entry->type==IFS_FS_ENTRY_DIRECTORY?EFI_FILE_DIRECTORY:0);
    for(size_t i=0;i<=units;i++)info->FileName[i]=(CHAR16)text[i];
    *bytes=required;return EFI_SUCCESS;
}
static EFI_STATUS new_file(V2Volume *,const char *,EFI_FILE_PROTOCOL **);
static EFI_STATUS V2_EFI open_file(EFI_FILE_PROTOCOL *this,EFI_FILE_PROTOCOL **out,
                                  CHAR16 *name,UINT64 mode,UINT64 attr)
{
    V2File *f=(V2File *)this;
    char path[BOOT_V2_PATH_BYTES];
    (void)attr;
    if(!out||!name)return EFI_INVALID_PARAMETER;
    *out=NULL;
    if(mode!=EFI_FILE_MODE_READ)return EFI_WRITE_PROTECTED;
    if(!live(f->volume))return EFI_MEDIA_CHANGED;
    if(f->type!=IFS_FS_ENTRY_DIRECTORY)return EFI_UNSUPPORTED;
    if(boot_v2_path(f->path,(const uint16_t *)name,path,sizeof(path)))return EFI_INVALID_PARAMETER;
    return new_file(f->volume,path,out);
}
static EFI_STATUS V2_EFI close_file(EFI_FILE_PROTOCOL *this)
{
    V2File *f=(V2File *)this;
    if(!f||!f->volume||!f->volume->opens)return EFI_INVALID_PARAMETER;
    --f->volume->opens;FreePool(f);return EFI_SUCCESS;
}
static EFI_STATUS V2_EFI delete_file(EFI_FILE_PROTOCOL *this)
{return close_file(this)==EFI_SUCCESS?EFI_WARN_DELETE_FAILURE:EFI_INVALID_PARAMETER;}
/* Directory reads return one EFI_FILE_INFO record. A short buffer must
 * leave the cursor unchanged so firmware can retry the same entry. */
static EFI_STATUS read_directory(V2File *f,UINTN *bytes,VOID *buffer)
{
    V2Volume *v=f->volume;
    char name[BOOT_V2_PATH_BYTES];IfsFsEntryV2 e;
    EFI_STATUS s;int rc;
    if(f->index==UINT32_MAX){*bytes=0;return EFI_VOLUME_CORRUPTED;}
    SetMem(name,sizeof(name),0xff);
    e=(IfsFsEntryV2){.structure_bytes=sizeof(e),.name=name,.name_capacity=sizeof(name)};
    rc=v->core->directory_entry(v->mounted,f->path,f->index,&e);
    if(rc==IFS_FS_END){*bytes=0;return EFI_SUCCESS;}
    if(rc!=IFS_FS_OK){*bytes=0;return result(rc);}
    if(!valid_entry(&e,name,sizeof(name))||!e.name_bytes){*bytes=0;return EFI_VOLUME_CORRUPTED;}
    for(uint32_t i=0;i<e.name_bytes;i++)if(name[i]=='/'||name[i]=='\\'||!name[i]){
        *bytes=0;return EFI_VOLUME_CORRUPTED;
    }
    s=fill_file(bytes,buffer,&e,name,e.name_bytes);
    if(s==EFI_SUCCESS)++f->index; /* retry short buffer at same entry */
    return s;

}

/* Regular-file reads alone own byte-position advancement and chunking.
 * Preserve the partial byte count if a provider reports an I/O failure. */
static EFI_STATUS read_regular_file(V2File *f,UINTN *bytes,VOID *buffer)
{
    V2Volume *v=f->volume;
    UINTN wanted,done=0;
    wanted=*bytes;*bytes=0;
    if(f->position>=f->size)return EFI_SUCCESS;
    if(wanted>f->size-f->position)wanted=(UINTN)(f->size-f->position);
    while(done<wanted){
        uint32_t n=(uint32_t)((wanted-done)>READ_CHUNK?READ_CHUNK:wanted-done);
        int rc=v->core->read(v->mounted,f->path,f->position,(UINT8 *)buffer+done,n);
        if(rc!=IFS_FS_OK){*bytes=done;return result(rc);}
        f->position+=n;done+=n;
    }
    *bytes=done;return EFI_SUCCESS;
}

/* The EFI callback validates the shared preconditions once, then dispatches
 * to the appropriate operation without mixing cursor semantics. */
static EFI_STATUS V2_EFI read_file(EFI_FILE_PROTOCOL *this,UINTN *bytes,VOID *buffer)
{
    V2File *f=(V2File *)this;
    if(!bytes||(!buffer&&*bytes))return EFI_INVALID_PARAMETER;
    if(!live(f->volume)){*bytes=0;return EFI_MEDIA_CHANGED;}
    if(f->type==IFS_FS_ENTRY_DIRECTORY)return read_directory(f,bytes,buffer);
    return read_regular_file(f,bytes,buffer);
}
static EFI_STATUS V2_EFI write_file(EFI_FILE_PROTOCOL *this,UINTN *bytes,VOID *buffer)
{(void)this;(void)buffer;if(bytes)*bytes=0;return EFI_WRITE_PROTECTED;}
static EFI_STATUS V2_EFI get_position(EFI_FILE_PROTOCOL *this,UINT64 *position)
{
    V2File *f=(V2File *)this;
    if(!position)return EFI_INVALID_PARAMETER;
    if(f->type==IFS_FS_ENTRY_DIRECTORY)return EFI_UNSUPPORTED;
    if(!live(f->volume))return EFI_MEDIA_CHANGED;
    *position=f->position;return EFI_SUCCESS;
}
static EFI_STATUS V2_EFI set_position(EFI_FILE_PROTOCOL *this,UINT64 position)
{
    V2File *f=(V2File *)this;
    if(!live(f->volume))return EFI_MEDIA_CHANGED;
    if(f->type==IFS_FS_ENTRY_DIRECTORY){
        if(position)return EFI_UNSUPPORTED;
        f->index=0;return EFI_SUCCESS;
    }
    f->position=position==UINT64_MAX?f->size:position;return EFI_SUCCESS;
}
static EFI_STATUS volume_info(V2Volume *v,BOOLEAN label_only,UINTN *bytes,VOID *buffer)
{
    const IfsFsVolumeOpsV2 *ops=(const IfsFsVolumeOpsV2 *)v->provider->module.groups[IFS_FS_GROUP_VOLUME];
    char label[BOOT_V2_PATH_BYTES]={0};uint16_t text[BOOT_V2_PATH_BYTES];
    uint32_t n=0;size_t units;UINTN required;CHAR16 *dest;
    if(ops && ops->label){
        int rc=ops->label(v->mounted,label,sizeof(label),&n);
        if(rc!=IFS_FS_OK)return result(rc);
        if(n>=sizeof(label)||label[n])return EFI_VOLUME_CORRUPTED;
    }
    if(boot_v2_utf16(label,n,text,BOOT_V2_PATH_BYTES,&units))return EFI_VOLUME_CORRUPTED;
    required=(label_only?0:SIZE_OF_EFI_FILE_SYSTEM_INFO)+(units+1)*sizeof(CHAR16);
    if(*bytes<required){*bytes=required;return EFI_BUFFER_TOO_SMALL;}
    if(!buffer)return EFI_INVALID_PARAMETER;
    SetMem(buffer,required,0);
    if(label_only)dest=(CHAR16 *)buffer;
    else {
        EFI_FILE_SYSTEM_INFO *i=buffer;
        i->Size=required;i->ReadOnly=TRUE;i->VolumeSize=v->info.total_bytes;
        i->FreeSpace=v->info.free_bytes;i->BlockSize=v->info.block_bytes;dest=i->VolumeLabel;
    }
    for(size_t j=0;j<=units;j++)dest[j]=(CHAR16)text[j];
    *bytes=required;return EFI_SUCCESS;
}
static EFI_STATUS V2_EFI get_info(EFI_FILE_PROTOCOL *this,EFI_GUID *guid,UINTN *bytes,VOID *buffer)
{
    V2File *f=(V2File *)this;
    IfsFsEntryV2 e;char name[BOOT_V2_PATH_BYTES];int rc;
    if(!guid||!bytes)return EFI_INVALID_PARAMETER;
    if(!live(f->volume))return EFI_MEDIA_CHANGED;
    if(!CompareMem(guid,&fs_guid,sizeof(*guid)))return volume_info(f->volume,FALSE,bytes,buffer);
    if(!CompareMem(guid,&label_guid,sizeof(*guid)))return volume_info(f->volume,TRUE,bytes,buffer);
    if(CompareMem(guid,&file_guid,sizeof(*guid)))return EFI_UNSUPPORTED;
    rc=stat(f->volume,f->path,&e,name);
    if(rc!=IFS_FS_OK)return result(rc);
    return fill_file(bytes,buffer,&e,name,(f->path[0]=='/'&&!f->path[1])?0:e.name_bytes);
}
static EFI_STATUS V2_EFI set_info(EFI_FILE_PROTOCOL *this,EFI_GUID *g,UINTN n,VOID *p)
{(void)this;(void)g;(void)n;(void)p;return EFI_WRITE_PROTECTED;}
static EFI_STATUS V2_EFI flush_file(EFI_FILE_PROTOCOL *this)
{(void)this;return EFI_WRITE_PROTECTED;}
static EFI_STATUS new_file(V2Volume *v,const char *path,EFI_FILE_PROTOCOL **out)
{
    V2File *f;IfsFsEntryV2 e;char name[BOOT_V2_PATH_BYTES],resolved[BOOT_V2_PATH_BYTES];int rc;size_t i;
    const IfsFsMetadataOpsV2 *meta=(const IfsFsMetadataOpsV2 *)v->provider->module.groups[IFS_FS_GROUP_METADATA];
    if(!out)return EFI_INVALID_PARAMETER;
    *out=NULL;
    if(!live(v))return EFI_MEDIA_CHANGED;
    rc=boot_v2_resolve(v->core,meta,v->mounted,path,resolved,&e,name);
    if(rc!=IFS_FS_OK)return result(rc);
    path=resolved;
    if(e.type!=IFS_FS_ENTRY_DIRECTORY&&e.type!=IFS_FS_ENTRY_FILE)return EFI_UNSUPPORTED;
    if(v->opens==(UINTN)-1)return EFI_OUT_OF_RESOURCES;
    f=AllocateZeroPool(sizeof(*f));if(!f)return EFI_OUT_OF_RESOURCES;
    for(i=0;path[i]&&i+1<sizeof(f->path);i++)f->path[i]=path[i];
    if(path[i]){FreePool(f);return EFI_INVALID_PARAMETER;}
    f->volume=v;f->type=e.type;f->size=e.size_bytes;
    f->file.Revision=EFI_FILE_HANDLE_REVISION;
    f->file.Open=CALLBACK(f->file.Open,open_file);
    f->file.Close=CALLBACK(f->file.Close,close_file);
    f->file.Delete=CALLBACK(f->file.Delete,delete_file);
    f->file.Read=CALLBACK(f->file.Read,read_file);
    f->file.Write=CALLBACK(f->file.Write,write_file);
    f->file.GetPosition=CALLBACK(f->file.GetPosition,get_position);
    f->file.SetPosition=CALLBACK(f->file.SetPosition,set_position);
    f->file.GetInfo=CALLBACK(f->file.GetInfo,get_info);
    f->file.SetInfo=CALLBACK(f->file.SetInfo,set_info);
    f->file.Flush=CALLBACK(f->file.Flush,flush_file);
    ++v->opens;*out=&f->file;return EFI_SUCCESS;
}
static EFI_STATUS V2_EFI open_volume(EFI_FILE_IO_INTERFACE *this,EFI_FILE_PROTOCOL **out)
{return new_file((V2Volume *)this,"/",out);}
BOOLEAN BootV2OwnsVolume(EFI_HANDLE handle)
{
    for(V2Volume *v=volumes;v;v=v->next)
        if(v->published&&v->storage.handle==handle)return TRUE;
    return FALSE;
}
BOOLEAN BootV2HasVolumes(VOID){return volumes!=NULL || BootV2HasProviders();}
static BOOLEAN retained_handle(EFI_HANDLE handle)
{
    for(V2Volume *v=volumes;v;v=v->next)
        if(v->storage.handle==handle)return TRUE;
    return FALSE;
}
static BOOLEAN valid_info(const V2Volume *v,uint32_t variant)
{
    return v->info.structure_bytes==sizeof(v->info)&&v->info.variant==variant&&
        v->info.total_bytes&&v->info.total_bytes<=v->storage.host.volume_bytes&&
        v->info.free_bytes<=v->info.total_bytes&&v->info.block_bytes&&
        v->info.max_name_bytes&&v->info.max_name_bytes<BOOT_V2_PATH_BYTES;
}
static void retain(V2Volume *v)
{v->next=volumes;volumes=v;++volume_count;++v->provider->mounted;}
/* A probe is a temporary host session, not a mounted filesystem. Keep
 * a positive candidate's exact media snapshot until ownership is resolved.
 * A provider that leaks allocations is retained for safety, never mounted. */
static EFI_STATUS discard_candidate(V2Volume *v,EFI_STATUS s)
{
    if(v->storage.storage.allocations){
        retain(v);
        LOG(1,LOG_LINE_NORMAL,L"Common V2 provider retained after allocation leak");
        return EFI_DEVICE_ERROR;
    }
    BootV2UefiStorageClose(&v->storage);FreePool(v);return s;
}
static EFI_STATUS probe_candidate(EFI_HANDLE handle,BootV2Provider *p,
                                  V2Volume **out,uint32_t *variant)
{
    V2Volume *v=AllocateZeroPool(sizeof(*v));EFI_STATUS s;int rc;
    *out=NULL;*variant=0;
    if(!v)return EFI_OUT_OF_RESOURCES;
    v->provider=p;v->core=boot_v2_module_readable_core(&p->module);
    if(!v->core){FreePool(v);return EFI_NOT_FOUND;}
    s=BootV2UefiStorageInit(&v->storage,handle);
    if(s!=EFI_SUCCESS){LOG(1,LOG_LINE_NORMAL,L"Common V2 storage initialization status=%r",s);FreePool(v);return s;}
    rc=v->core->probe(&v->storage.host,variant);
    LOG(1,LOG_LINE_NORMAL,L"Common V2 probe status=%d variant=%d",rc,*variant);
    if(v->storage.storage.allocations)
        return discard_candidate(v,EFI_DEVICE_ERROR);
    /* Unrecognised, unsupported or invalid format does not establish a
     * claim. A host I/O or resource failure prevents a complete decision. */
    if(rc==IFS_FS_NOT_FOUND||rc==IFS_FS_UNSUPPORTED||rc==IFS_FS_INVALID||rc==IFS_FS_CORRUPT)
        return discard_candidate(v,EFI_NOT_FOUND);
    if(rc!=IFS_FS_OK)return discard_candidate(v,result(rc));
    if(boot_v2_storage_read(&v->storage.storage,0,NULL,0))
        return discard_candidate(v,EFI_MEDIA_CHANGED);
    *out=v;return EFI_SUCCESS;
}
/* Mounting and root validation are independent of firmware protocol
 * publication. On failure the caller still owns the candidate session. */
static EFI_STATUS mount_candidate(V2Volume *v,uint32_t variant)
{
    int rc;
    IfsFsEntryV2 root;
    char name[BOOT_V2_PATH_BYTES];
    if(boot_v2_storage_read(&v->storage.storage,0,NULL,0))
        return EFI_MEDIA_CHANGED;
    v->info.structure_bytes=sizeof(v->info);
    rc=v->core->mount(&v->storage.host,&v->mounted,&v->info);
    LOG(1,LOG_LINE_NORMAL,L"Common V2 mount status=%d variant=%d maxname=%d total=%lx free=%lx",rc,v->info.variant,v->info.max_name_bytes,v->info.total_bytes,v->info.free_bytes);
    if(rc!=IFS_FS_OK||!v->mounted||!valid_info(v,variant))
        return rc==IFS_FS_OK?EFI_VOLUME_CORRUPTED:result(rc);

    rc=stat(v,"/",&root,name);
    LOG(1,LOG_LINE_NORMAL,L"Common V2 root stat status=%d type=%d",rc,rc==IFS_FS_OK?root.type:0);
    if(rc!=IFS_FS_OK||root.type!=IFS_FS_ENTRY_DIRECTORY)return EFI_VOLUME_CORRUPTED;
    return EFI_SUCCESS;
}

/* Publication is the only stage that registers a new EFI interface.
 * An existing firmware filesystem always retains priority. */
static EFI_STATUS publish_volume(V2Volume *v)
{
    EFI_HANDLE handle=v->storage.handle;
    EFI_STATUS s;
    VOID *existing=NULL;

    s=boot_call3_wrapper(BS->HandleProtocol,handle,&sfs_guid,&existing);
    if(s==EFI_SUCCESS)return EFI_ALREADY_STARTED;
    if(s!=EFI_UNSUPPORTED&&s!=EFI_NOT_FOUND)return s;

    v->sfs.Revision=EFI_FILE_IO_INTERFACE_REVISION;
    v->sfs.OpenVolume=CALLBACK(v->sfs.OpenVolume,open_volume);
    s=boot_call4_wrapper(BS->InstallProtocolInterface,&handle,&sfs_guid,EFI_NATIVE_INTERFACE,&v->sfs);
    LOG(1,LOG_LINE_NORMAL,L"Common V2 SimpleFS publication status=%r",s);
    if(s==EFI_SUCCESS)v->published=TRUE;
    return s;
}

/* The caller transfers ownership of the candidate to this coordinator.
 * Success retains it as a published volume; failure unwinds the mount
 * and storage session unless the provider still holds resources. */
static EFI_STATUS try_mount(V2Volume *v,uint32_t variant)
{
    EFI_STATUS s=mount_candidate(v,variant);
    if(s==EFI_SUCCESS)s=publish_volume(v);
    if(s==EFI_SUCCESS){
        retain(v);
        return EFI_SUCCESS;
    }
    if(v->mounted&&v->core->unmount(v->mounted)!=IFS_FS_OK){
        /* Do not free a host object still borrowed by provider code. */
        retain(v);
        return EFI_DEVICE_ERROR;
    }
    v->mounted=NULL;
    return discard_candidate(v,s);
}
/* All positive probes must agree on one owner. Do not let sorted module
 * names, a failed mount, or the order of files on the ESP choose a format.
 * A pre-existing EFI filesystem protocol has already had first refusal. */
static EFI_STATUS select_and_mount(EFI_HANDLE handle)
{
    V2Volume *selected=NULL;
    uint32_t selected_variant=0;
    for(BootV2Provider *p=BootV2Providers();p;p=p->next){
        V2Volume *candidate=NULL;uint32_t variant=0;EFI_STATUS s;
        if(p->memory.memory.quarantined)continue;
        s=probe_candidate(handle,p,&candidate,&variant);
        if(s==EFI_NOT_FOUND)continue;
        if(s!=EFI_SUCCESS){
            if(selected)discard_candidate(selected,s);
            return s;
        }
        if(selected){
            LOG(1,LOG_LINE_NORMAL,L"Common V2 ambiguous filesystem ownership on handle %lx",(UINT64)(UINTN)handle);
            discard_candidate(candidate,EFI_ACCESS_DENIED);
            discard_candidate(selected,EFI_ACCESS_DENIED);
            return EFI_ACCESS_DENIED;
        }
        selected=candidate;selected_variant=variant;
    }
    return selected?try_mount(selected,selected_variant):EFI_NOT_FOUND;
}
UINTN BootV2MountVolumes(VOID)
{
    UINTN count=0;
    if(!BootV2DiscoverProviders())return 0;
    for(UINTN i=0;i<VolumesCount&&volume_count<MAX_VOLUMES;i++){
        BOOT_VOLUME *vol=Volumes[i];
        if(vol)LOG(1,LOG_LINE_NORMAL,L"Common V2 candidate %d: %s handle=%lx readable=%d partition=%d",i,vol->VolName,(UINT64)(UINTN)vol->DeviceHandle,vol->IsReadable,vol->BlockIO&&vol->BlockIO->Media?vol->BlockIO->Media->LogicalPartition:0);
        if(!vol||!vol->DeviceHandle||vol->IsReadable||!vol->BlockIO||!vol->BlockIO->Media||
           !vol->BlockIO->Media->LogicalPartition||BootVolumeExcluded(vol)||retained_handle(vol->DeviceHandle))continue;
        if(select_and_mount(vol->DeviceHandle)==EFI_SUCCESS){
            ++count;LOG(1,LOG_LINE_NORMAL,L"Common V2 mounted volume on handle %lx",(UINT64)(UINTN)vol->DeviceHandle);
        }
    }
    return count;
}
/* Tear down one volume in strict dependency order. Each completed stage
 * is recorded, allowing a later shutdown to resume without double release.
 * The caller unlinks/frees the object only after this succeeds. */
static EFI_STATUS release_volume(V2Volume *v)
{
    if(v->opens)return EFI_ACCESS_DENIED;
    if(v->published){
        EFI_STATUS s=boot_call3_wrapper(BS->UninstallProtocolInterface,v->storage.handle,&sfs_guid,&v->sfs);
        if(s!=EFI_SUCCESS)return s;
        v->published=FALSE;
    }
    if(v->mounted){
        if(v->core->unmount(v->mounted)!=IFS_FS_OK)return EFI_DEVICE_ERROR;
        v->mounted=NULL;
    }
    if(v->storage.storage.allocations)return EFI_DEVICE_ERROR;
    BootV2UefiStorageClose(&v->storage);
    return EFI_SUCCESS;
}

EFI_STATUS BootV2Shutdown(VOID)
{
    V2Volume **at=&volumes;
    EFI_STATUS rc=EFI_SUCCESS;
    while(*at){
        V2Volume *v=*at;
        EFI_STATUS s=release_volume(v);
        if(s!=EFI_SUCCESS){
            rc=s;
            at=&v->next;
            continue;
        }
        --v->provider->mounted;
        --volume_count;
        *at=v->next;
        FreePool(v);
    }
    if(rc==EFI_SUCCESS)rc=BootV2ReleaseProviders();
    return rc;
}
