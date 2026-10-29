/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Boot-owned, format-neutral bridge: a verified DSO supplies all filesystem
 * interpretation. Child handles preserve firmware's original filesystem and
 * storage drivers. Nothing is disconnected, reinstalled or written to disk. */
#include "so_uefi.h"
#include "so_elf.h"
#ifdef __MAKEWITH_GNUEFI
#include <efilib.h>
#endif

#if defined(EFIX64)
#ifndef EFI_MEMORY_RO
#define EFI_MEMORY_RO UINT64_C(0x20000)
#endif
#define SO_MAX_VOLUMES 32u
#define SO_MAX_MODULES 8u
#define SO_PATH_BYTES 4096u
#define CONTAINER(pointer,type,member) ((type *)((UINT8 *)(pointer)-offsetof(type,member)))
static EFI_GUID block_guid=EFI_BLOCK_IO_PROTOCOL_GUID;
static EFI_GUID filesystem_guid=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
static EFI_GUID device_path_guid=EFI_DEVICE_PATH_PROTOCOL_GUID;
static EFI_GUID file_info_guid=EFI_FILE_INFO_ID;
static EFI_GUID filesystem_info_guid=EFI_FILE_SYSTEM_INFO_ID;
static EFI_GUID volume_label_guid=EFI_FILE_SYSTEM_VOLUME_LABEL_ID;
/* Presence identifies a Boot SO child, not an interchangeable native mount. */
static EFI_GUID so_guid={0xd84d8f78,0xa124,0x4e74,{0x9f,0x88,0xa7,0x48,0x40,0xbd,0x5b,0x11}};
static EFI_GUID global_guid=EFI_GLOBAL_VARIABLE;
static BOOLEAN diagnostics;
static EFI_BOOT_SERVICES *services;
/* UEFI 2.10 section 37.7.1. Keep this optional for older firmware; Secure
 * Boot still rejects unsigned modules regardless of protocol availability. */
typedef struct BootMemoryAttributes BootMemoryAttributes;
typedef EFI_STATUS (EFIAPI *MemoryAttributesGet)(BootMemoryAttributes *,EFI_PHYSICAL_ADDRESS,UINT64,UINT64 *);
typedef EFI_STATUS (EFIAPI *MemoryAttributesChange)(BootMemoryAttributes *,EFI_PHYSICAL_ADDRESS,UINT64,UINT64);
struct BootMemoryAttributes {
    MemoryAttributesGet Get;
    MemoryAttributesChange Set, Clear;
};
static BootMemoryAttributes *memory_attributes;
static EFI_GUID memory_attributes_guid={0xf4560cf6,0x40ec,0x4b4a,{0xa1,0x92,0xbf,0x1d,0x57,0xd0,0xb1,0x89}};

typedef struct SoVolume SoVolume;
typedef struct {
    EFI_FILE_PROTOCOL protocol;
    SoVolume *volume;
    void *context;
    IfsBootInfo info;
    UINT64 position, read_bytes;
    UINT32 directory_index;
    char path[SO_PATH_BYTES];
} SoFile;
struct SoVolume {
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL filesystem;
    EFI_BLOCK_IO_PROTOCOL block;
    EFI_BLOCK_IO_MEDIA media;
    EFI_BLOCK_IO_PROTOCOL *parent_block;
    EFI_HANDLE parent, child;
    EFI_DEVICE_PATH *path;
    BootSoImage image;
    IfsBootIo io;
    CHAR16 label[IFS_BOOT_NAME_BYTES];
    VOID *bounce_allocation, *bounce;
    SoVolume *next;
};
typedef struct {
    VOID *bytes;
    UINTN length;
    IfsBootManifest manifest;
} SoModule;
static SoVolume *volumes;
static SoModule modules[SO_MAX_MODULES];
static UINTN module_count, volume_count;

static VOID copy_bytes(VOID *d, const VOID *s, UINTN n) {
    UINT8 *out=d; const UINT8 *in=s;
    while (n--) *out++=*in++;
}
static VOID clear_bytes(VOID *d, UINTN n) { UINT8 *p=d; while(n--) *p++=0; }
static BOOLEAN guid_equal(const EFI_GUID *a, const EFI_GUID *b) {
    const UINT8 *x=(const UINT8 *)a, *y=(const UINT8 *)b;
    for (UINTN i=0;i<sizeof(*a);++i) if(x[i]!=y[i]) return FALSE;
    return TRUE;
}
static VOID *pool(UINTN bytes) {
    VOID *p=NULL;
    if (EFI_ERROR(services->AllocatePool(EfiBootServicesData,bytes,&p))) return NULL;
    clear_bytes(p,bytes); return p;
}
static VOID free_pool(VOID *p) { if(p) services->FreePool(p); }
static void *image_allocate(void *u, size_t bytes) {
    (void)u;
    EFI_PHYSICAL_ADDRESS address=0;
    if (EFI_ERROR(services->AllocatePages(AllocateAnyPages,
                                      EfiLoaderCode,bytes/4096u,&address))) return NULL;
    if (memory_attributes &&
        (EFI_ERROR(memory_attributes->Set(memory_attributes,address,bytes,EFI_MEMORY_XP)) ||
         EFI_ERROR(memory_attributes->Clear(memory_attributes,address,bytes,EFI_MEMORY_RO|EFI_MEMORY_RP)))) {
        services->FreePages(address,bytes/4096u); return NULL;
    }
    return (VOID *)(UINTN)address;
}
static void image_release(void *u, void *p, size_t bytes) {
    (void)u;
    if(memory_attributes) {
        (void)memory_attributes->Set(memory_attributes,(EFI_PHYSICAL_ADDRESS)(UINTN)p,bytes,EFI_MEMORY_XP);
        (void)memory_attributes->Clear(memory_attributes,(EFI_PHYSICAL_ADDRESS)(UINTN)p,bytes,EFI_MEMORY_RO|EFI_MEMORY_RP);
    }
    services->FreePages((EFI_PHYSICAL_ADDRESS)(UINTN)p,bytes/4096u);
}
static int image_protect(void *u, void *p, size_t bytes, uint32_t flags) {
    (void)u;
    if(!memory_attributes) return 0;
    UINT64 set=(flags&BOOT_SO_WRITE)?0:EFI_MEMORY_RO;
    UINT64 clear=(flags&BOOT_SO_WRITE)?EFI_MEMORY_RO:0;
    if(flags&BOOT_SO_EXECUTE) clear|=EFI_MEMORY_XP;
    else set|=EFI_MEMORY_XP;
    EFI_PHYSICAL_ADDRESS address=(EFI_PHYSICAL_ADDRESS)(UINTN)p;
    if(set && EFI_ERROR(memory_attributes->Set(memory_attributes,address,bytes,set))) return -1;
    if(clear && EFI_ERROR(memory_attributes->Clear(memory_attributes,address,bytes,clear))) return -1;
    return 0;
}
static EFI_STATUS from_result(int status) {
    if(status==IFS_BOOT_OK) return EFI_SUCCESS;
    if(status==IFS_BOOT_NOT_FOUND) return EFI_NOT_FOUND;
    if(status==IFS_BOOT_IO_ERROR) return EFI_DEVICE_ERROR;
    return EFI_VOLUME_CORRUPTED;
}
/* UTF conversion rejects malformed encodings and drive-selector injection.
 * All module paths are absolute, canonical, volume-relative UTF-8. */
static BOOLEAN utf16_append(char *out, UINTN *length, const CHAR16 *s) {
    for (UINTN i=0;i<SO_PATH_BYTES && s[i];++i) {
        UINT32 c=s[i];
        if(c>=0xd800u && c<=0xdbffu) {
            if(i+1u>=SO_PATH_BYTES) return FALSE;
            UINT32 low=s[++i];
            if(low<0xdc00u || low>0xdfffu) return FALSE;
            c=0x10000u+((c-0xd800u)<<10)+(low-0xdc00u);
        } else if(c>=0xdc00u && c<=0xdfffu) return FALSE;
        if(c==':' || c<32u) return FALSE;
        if(c=='\\') c='/';
        UINTN n=c<0x80u?1u:c<0x800u?2u:c<0x10000u?3u:4u;
        if(*length+n>=SO_PATH_BYTES) return FALSE;
        for(UINTN j=n;j>1;--j) { out[*length+j-1u]=(char)(0x80u|(c&63u)); c>>=6; }
        out[*length]=(char)(n==1?c:(n==2?0xc0u:n==3?0xe0u:0xf0u)|c);
        *length+=n;
        out[*length]=0;
        if(i+1u==SO_PATH_BYTES && s[i+1u]) return FALSE;
    }
    return TRUE;
}
static BOOLEAN make_path(SoFile *parent, const CHAR16 *name, char *out) {
    char raw[SO_PATH_BYTES];
    UINTN n=0;
    if(!name) return FALSE;
    if(name[0]!='/' && name[0]!='\\') {
        while(parent->path[n]) { raw[n]=parent->path[n]; ++n; }
        if(n+1u>=sizeof(raw)) return FALSE;
        if(!n || raw[n-1u]!='/') raw[n++]='/';
    }
    raw[n]=0;
    if(!utf16_append(raw,&n,name)) return FALSE;
    UINTN position=0, length=1;
    out[0]='/'; out[1]=0;
    while(position<n) {
        while(position<n && raw[position]=='/') ++position;
        UINTN start=position;
        while(position<n && raw[position]!='/') ++position;
        UINTN count=position-start;
        if(!count || (count==1 && raw[start]=='.')) continue;
        if(count==2 && raw[start]=='.' && raw[start+1]=='.') {
            while(length>1 && out[length-1]!='/') --length;
            if(length>1) --length;
            out[length]=0; continue;
        }
        if(length>1) out[length++]='/';
        if(count>=SO_PATH_BYTES-length) return FALSE;
        copy_bytes(out+length,raw+start,count); length+=count; out[length]=0;
    }
    return TRUE;
}
static UINTN utf8_name(const char *s, CHAR16 *out, UINTN capacity) {
    UINTN count=0, i=0;
    while(i<IFS_BOOT_NAME_BYTES && s[i]) {
        UINT32 c=(UINT8)s[i++], minimum=0;
        UINTN extra=0;
        if(c>=0xf0u && c<=0xf4u) { c&=7u; extra=3; minimum=0x10000u; }
        else if(c>=0xe0u && c<=0xefu) { c&=15u; extra=2; minimum=0x800u; }
        else if(c>=0xc2u && c<=0xdfu) { c&=31u; extra=1; minimum=0x80u; }
        else if(c>=0x80u) return 0;
        while(extra--) {
            if(i>=IFS_BOOT_NAME_BYTES || ((UINT8)s[i]&0xc0u)!=0x80u) return 0;
            c=(c<<6)|((UINT8)s[i++]&63u);
        }
        if(c<minimum || c>0x10ffffu || (c>=0xd800u && c<=0xdfffu)) return 0;
        if(count+(c>=0x10000u?2u:1u)>=capacity) return 0;
        if(c>=0x10000u) { c-=0x10000u; out[count++]=(CHAR16)(0xd800u+(c>>10)); out[count++]=(CHAR16)(0xdc00u+(c&1023u)); }
        else out[count++]=(CHAR16)c;
    }
    if(i==IFS_BOOT_NAME_BYTES) return 0;
    out[count]=0; return count+1u;
}

static EFI_STATUS EFIAPI file_open(EFI_FILE_PROTOCOL *, EFI_FILE_PROTOCOL **,
                                    CHAR16 *, UINT64, UINT64);
static EFI_STATUS EFIAPI file_close(EFI_FILE_PROTOCOL *);
static EFI_STATUS EFIAPI file_delete(EFI_FILE_PROTOCOL *);
static EFI_STATUS EFIAPI file_read(EFI_FILE_PROTOCOL *, UINTN *, VOID *);
static EFI_STATUS EFIAPI file_write(EFI_FILE_PROTOCOL *, UINTN *, VOID *);
static EFI_STATUS EFIAPI file_get_position(EFI_FILE_PROTOCOL *, UINT64 *);
static EFI_STATUS EFIAPI file_set_position(EFI_FILE_PROTOCOL *, UINT64);
static EFI_STATUS EFIAPI file_get_info(EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN *, VOID *);
static EFI_STATUS EFIAPI file_set_info(EFI_FILE_PROTOCOL *, EFI_GUID *, UINTN, VOID *);
static EFI_STATUS EFIAPI file_flush(EFI_FILE_PROTOCOL *);
static EFI_STATUS new_file(SoVolume *volume, const char *path, EFI_FILE_PROTOCOL **out) {
    *out=NULL;
    SoFile *file=pool(sizeof(*file));
    if(!file) return EFI_OUT_OF_RESOURCES;
    file->context=pool(volume->image.api->file_context_bytes);
    if(!file->context) { free_pool(file); return EFI_OUT_OF_RESOURCES; }
    EFI_STATUS status=from_result(volume->image.api->open(path,file->context,&file->info));
    if(EFI_ERROR(status)) { free_pool(file->context); free_pool(file); return status; }
    UINTN i=0; while(path[i]) { file->path[i]=path[i]; ++i; } file->path[i]=0;
    file->volume=volume;
    file->protocol.Revision=EFI_FILE_PROTOCOL_REVISION;
    file->protocol.Open=file_open; file->protocol.Close=file_close;
    file->protocol.Delete=file_delete; file->protocol.Read=file_read;
    file->protocol.Write=file_write; file->protocol.GetPosition=file_get_position;
    file->protocol.SetPosition=file_set_position; file->protocol.GetInfo=file_get_info;
    file->protocol.SetInfo=file_set_info; file->protocol.Flush=file_flush;
    *out=&file->protocol;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI open_volume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *this,
                                      EFI_FILE_PROTOCOL **root) {
    if(!this || !root) return EFI_INVALID_PARAMETER;
    return new_file(CONTAINER(this,SoVolume,filesystem),"/",root);
}
static EFI_STATUS EFIAPI file_open(EFI_FILE_PROTOCOL *this, EFI_FILE_PROTOCOL **out,
                                    CHAR16 *name, UINT64 mode, UINT64 attributes) {
    (void)attributes;
    if(!this || !out || !name) return EFI_INVALID_PARAMETER;
    *out=NULL;
    if(mode!=EFI_FILE_MODE_READ) return EFI_WRITE_PROTECTED;
    SoFile *parent=(SoFile *)this;
    if(!(parent->info.attributes&IFS_BOOT_DIRECTORY)) return EFI_UNSUPPORTED;
    char path[SO_PATH_BYTES];
    if(!make_path(parent,name,path)) return EFI_INVALID_PARAMETER;
    return new_file(parent->volume,path,out);
}
static EFI_STATUS EFIAPI file_close(EFI_FILE_PROTOCOL *this) {
    if(!this) return EFI_INVALID_PARAMETER;
    SoFile *file=(SoFile *)this;
    EFI_STATUS status=from_result(file->volume->image.api->close(file->context));
    if(diagnostics && file->read_bytes) {
        CHAR16 path[256];
        UINTN i=0; while(file->path[i] && i+1u<256u) { path[i]=(UINT8)file->path[i]; ++i; }
        path[i]=0;
        Print(L"[Boot SO] close %s: %ld bytes read through module\n",path,file->read_bytes);
    }
    free_pool(file->context); free_pool(file); return status;
}
static EFI_STATUS EFIAPI file_delete(EFI_FILE_PROTOCOL *this) {
    if(!this) return EFI_INVALID_PARAMETER;
    (void)file_close(this); return EFI_WARN_DELETE_FAILURE;
}
static EFI_STATUS fill_file_info(const IfsBootInfo *source, UINTN *bytes, VOID *buffer) {
    CHAR16 name[IFS_BOOT_NAME_BYTES];
    UINTN chars=utf8_name(source->name,name,IFS_BOOT_NAME_BYTES);
    if(!chars) return EFI_VOLUME_CORRUPTED;
    UINTN required=SIZE_OF_EFI_FILE_INFO+chars*sizeof(CHAR16);
    if(*bytes<required) { *bytes=required; return EFI_BUFFER_TOO_SMALL; }
    if(!buffer) return EFI_INVALID_PARAMETER;
    clear_bytes(buffer,required);
    EFI_FILE_INFO *info=buffer;
    info->Size=required; info->FileSize=source->bytes; info->PhysicalSize=source->bytes;
    info->Attribute=source->attributes&EFI_FILE_VALID_ATTR;
    info->CreateTime.Year=info->LastAccessTime.Year=info->ModificationTime.Year=1980;
    info->CreateTime.Month=info->LastAccessTime.Month=info->ModificationTime.Month=1;
    info->CreateTime.Day=info->LastAccessTime.Day=info->ModificationTime.Day=1;
    copy_bytes(info->FileName,name,chars*sizeof(CHAR16)); *bytes=required;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_read(EFI_FILE_PROTOCOL *this, UINTN *bytes, VOID *buffer) {
    if(!this || !bytes || (!buffer && *bytes)) return EFI_INVALID_PARAMETER;
    SoFile *file=(SoFile *)this;
    if(file->info.attributes&IFS_BOOT_DIRECTORY) {
        IfsBootInfo info;
        int result=file->volume->image.api->directory_entry(file->context,file->directory_index,&info);
        if(result==IFS_BOOT_END) { *bytes=0; return EFI_SUCCESS; }
        if(result!=IFS_BOOT_OK) return from_result(result);
        EFI_STATUS status=fill_file_info(&info,bytes,buffer);
        if(!EFI_ERROR(status)) ++file->directory_index;
        return status;
    }
    UINTN requested=*bytes;
    if(file->position>=file->info.bytes) { *bytes=0; return EFI_SUCCESS; }
    if(requested>file->info.bytes-file->position) requested=(UINTN)(file->info.bytes-file->position);
    *bytes=0;
    while(*bytes<requested) {
        UINTN chunk=requested-*bytes;
        if(chunk>1024u*1024u) chunk=1024u*1024u;
        UINT32 got=0;
        EFI_STATUS status=from_result(file->volume->image.api->read(file->context,file->position,
                                           (UINT8 *)buffer+*bytes,(UINT32)chunk,&got));
        if(got>chunk) return EFI_VOLUME_CORRUPTED;
        *bytes+=got; file->position+=got; file->read_bytes+=got;
        if(EFI_ERROR(status)) return status;
        if(!got) return EFI_VOLUME_CORRUPTED;
    }
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_write(EFI_FILE_PROTOCOL *this, UINTN *bytes, VOID *buffer) {
    (void)this; (void)buffer; if(bytes) *bytes=0; return EFI_WRITE_PROTECTED;
}
static EFI_STATUS EFIAPI file_get_position(EFI_FILE_PROTOCOL *this, UINT64 *position) {
    if(!this || !position) return EFI_INVALID_PARAMETER;
    SoFile *file=(SoFile *)this;
    if(file->info.attributes&IFS_BOOT_DIRECTORY) return EFI_UNSUPPORTED;
    *position=file->position; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_set_position(EFI_FILE_PROTOCOL *this, UINT64 position) {
    if(!this) return EFI_INVALID_PARAMETER;
    SoFile *file=(SoFile *)this;
    if(file->info.attributes&IFS_BOOT_DIRECTORY) {
        if(position) return EFI_UNSUPPORTED;
        file->directory_index=0; return EFI_SUCCESS;
    }
    file->position=position==~(UINT64)0?file->info.bytes:position;
    return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_get_info(EFI_FILE_PROTOCOL *this, EFI_GUID *type,
                                       UINTN *bytes, VOID *buffer) {
    if(!this || !type || !bytes) return EFI_INVALID_PARAMETER;
    SoFile *file=(SoFile *)this;
    if(guid_equal(type,&file_info_guid)) return fill_file_info(&file->info,bytes,buffer);
    BOOLEAN fs=guid_equal(type,&filesystem_info_guid);
    if(!fs && !guid_equal(type,&volume_label_guid)) return EFI_UNSUPPORTED;
    const CHAR16 *label=file->volume->label;
    UINTN chars=0; while(label[chars]) ++chars;
    ++chars;
    UINTN required=(fs?SIZE_OF_EFI_FILE_SYSTEM_INFO:0u)+chars*sizeof(CHAR16);
    if(*bytes<required) { *bytes=required; return EFI_BUFFER_TOO_SMALL; }
    if(!buffer) return EFI_INVALID_PARAMETER;
    clear_bytes(buffer,required);
    if(fs) {
        EFI_FILE_SYSTEM_INFO *info=buffer;
        info->Size=required; info->ReadOnly=TRUE;
        info->VolumeSize=file->volume->io.media_bytes;
        info->BlockSize=file->volume->io.block_bytes;
        copy_bytes(info->VolumeLabel,label,chars*sizeof(CHAR16));
    } else copy_bytes(buffer,label,chars*sizeof(CHAR16));
    *bytes=required; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI file_set_info(EFI_FILE_PROTOCOL *this, EFI_GUID *type,
                                       UINTN bytes, VOID *buffer) {
    (void)this; (void)type; (void)bytes; (void)buffer; return EFI_WRITE_PROTECTED;
}
static EFI_STATUS EFIAPI file_flush(EFI_FILE_PROTOCOL *this) {
    return this?EFI_SUCCESS:EFI_INVALID_PARAMETER;
}

static int IFS_BOOT_CALL disk_read(void *context, uint64_t offset, void *out, uint32_t bytes) {
    SoVolume *volume=context;
    EFI_BLOCK_IO_PROTOCOL *disk=volume->parent_block;
    if(!disk->Media || !disk->Media->MediaPresent || disk->Media->MediaId!=volume->media.MediaId ||
        disk->Media->BlockSize!=volume->media.BlockSize || offset>volume->io.media_bytes ||
        bytes>volume->io.media_bytes-offset) return -1;
    UINT32 block=volume->media.BlockSize;
    UINT8 *destination=out;
    while(bytes) {
        UINT32 skip=(UINT32)(offset%block);
        UINT32 wanted=bytes>65536u?65536u:bytes;
        UINT32 transferred=skip?block:(wanted/block)*block;
        if(!transferred) transferred=block;
        EFI_STATUS status=disk->ReadBlocks(disk,volume->media.MediaId,
                                              offset/block,transferred,volume->bounce);
        if(EFI_ERROR(status)) return -1;
        UINT32 copied=transferred-skip;
        if(copied>bytes) copied=bytes;
        copy_bytes(destination,(UINT8 *)volume->bounce+skip,copied);
        destination+=copied; offset+=copied; bytes-=copied;
    }
    return 0;
}
static EFI_STATUS EFIAPI block_reset(EFI_BLOCK_IO_PROTOCOL *this, BOOLEAN extended) {
    (void)this; (void)extended; return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI block_read(EFI_BLOCK_IO_PROTOCOL *this, UINT32 media,
                                     EFI_LBA lba, UINTN bytes, VOID *buffer) {
    SoVolume *volume=CONTAINER(this,SoVolume,block);
    if(media!=volume->media.MediaId) return EFI_MEDIA_CHANGED;
    if(bytes>UINT32_MAX || lba>volume->media.LastBlock ||
        bytes%volume->media.BlockSize || (!buffer && bytes)) return EFI_INVALID_PARAMETER;
    return disk_read(volume,lba*volume->media.BlockSize,buffer,(UINT32)bytes)==0?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
static EFI_STATUS EFIAPI block_write(EFI_BLOCK_IO_PROTOCOL *this, UINT32 media,
                                      EFI_LBA lba, UINTN bytes, VOID *buffer) {
    (void)this; (void)media; (void)lba; (void)bytes; (void)buffer; return EFI_WRITE_PROTECTED;
}
static EFI_STATUS EFIAPI block_flush(EFI_BLOCK_IO_PROTOCOL *this) { (void)this; return EFI_SUCCESS; }
BOOLEAN BootSoOwnsParent(EFI_HANDLE handle) {
    for(SoVolume *v=volumes;v;v=v->next) if(v->parent==handle) return TRUE;
    return FALSE;
}
BOOLEAN BootSoHandleMatches(EFI_HANDLE handle, const CHAR16 *id) {
    if(!id || !id[0]) return FALSE;
    for(SoVolume *v=volumes;v;v=v->next) {
        if(v->child!=handle) continue;
        for(UINTN i=0;i<16u;++i) {
            if(id[i]!=(UINT8)v->image.manifest.format_id[i]) return FALSE;
            if(!id[i]) return TRUE;
        }
    }
    return FALSE;
}
static EFI_STATUS bind_module(SoModule *module, EFI_HANDLE handle) {
    EFI_BLOCK_IO_PROTOCOL *disk=NULL;
    EFI_DEVICE_PATH *parent_path=NULL;
    VOID *existing=NULL;
    if(BootSoOwnsParent(handle) || volume_count>=SO_MAX_VOLUMES) return EFI_ALREADY_STARTED;
    if(!EFI_ERROR(services->HandleProtocol(handle,&so_guid,&existing))) return EFI_ALREADY_STARTED;
    EFI_STATUS status=services->HandleProtocol(handle,&block_guid,(VOID **)&disk);
    if(EFI_ERROR(status) || !disk || !disk->Media || !disk->Media->MediaPresent ||
        disk->Media->BlockSize<512u || disk->Media->BlockSize>4096u ||
        (disk->Media->BlockSize&(disk->Media->BlockSize-1u)) ||
        disk->Media->LastBlock==~(UINT64)0 ||
        disk->Media->LastBlock+1u>UINT64_MAX/disk->Media->BlockSize ||
        disk->Media->IoAlign>65536u ||
        (disk->Media->IoAlign && (disk->Media->IoAlign&(disk->Media->IoAlign-1u)))) return EFI_UNSUPPORTED;
    status=services->HandleProtocol(handle,&device_path_guid,(VOID **)&parent_path);
    if(EFI_ERROR(status) || !parent_path) return EFI_UNSUPPORTED;
    SoVolume *volume=pool(sizeof(*volume));
    if(!volume) return EFI_OUT_OF_RESOURCES;
    BootSoMemory memory={NULL,image_allocate,image_release,image_protect};
    if(BootSoLoad(module->bytes,module->length,&memory,&volume->image)!=0) {
        free_pool(volume); return EFI_LOAD_ERROR;
    }
    volume->parent=handle; volume->parent_block=disk; volume->media=*disk->Media;
    volume->io=(IfsBootIo){volume,(disk->Media->LastBlock+1u)*disk->Media->BlockSize,
                           disk->Media->BlockSize,sizeof(IfsBootIo),disk_read};
    volume->bounce_allocation=pool(65536u+65536u);
    if(!volume->bounce_allocation) { status=EFI_OUT_OF_RESOURCES; goto failure; }
    UINTN align=disk->Media->IoAlign>1u?disk->Media->IoAlign:1u;
    volume->bounce=(VOID *)(((UINTN)volume->bounce_allocation+align-1u)&~(align-1u));
    if(volume->image.api->mount(&volume->io)!=0) { status=EFI_UNSUPPORTED; goto failure; }
    char label[IFS_BOOT_NAME_BYTES];
    if(volume->image.api->volume_label(label,sizeof(label))!=0) { status=EFI_VOLUME_CORRUPTED; goto unmount; }
    if(!label[0]) copy_bytes(label,volume->image.manifest.display_name,sizeof(volume->image.manifest.display_name));
    if(!utf8_name(label,volume->label,IFS_BOOT_NAME_BYTES)) { status=EFI_VOLUME_CORRUPTED; goto unmount; }
    VENDOR_DEVICE_PATH node;
    clear_bytes(&node,sizeof(node));
    node.Header.Type=MEDIA_DEVICE_PATH; node.Header.SubType=MEDIA_VENDOR_DP;
    SetDevicePathNodeLength(&node.Header,sizeof(node)); node.Guid=so_guid;
    volume->path=AppendDevicePathNode(parent_path,&node.Header);
    if(!volume->path) { status=EFI_OUT_OF_RESOURCES; goto unmount; }
    volume->filesystem.Revision=EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_REVISION;
    volume->filesystem.OpenVolume=open_volume;
    volume->media.ReadOnly=TRUE;
    volume->block.Revision=disk->Revision; volume->block.Media=&volume->media;
    volume->block.Reset=block_reset; volume->block.ReadBlocks=block_read;
    volume->block.WriteBlocks=block_write; volume->block.FlushBlocks=block_flush;
    status=services->InstallMultipleProtocolInterfaces(&volume->child,
                                &filesystem_guid,&volume->filesystem,&block_guid,&volume->block,
                                &device_path_guid,volume->path,&so_guid,volume,NULL);
    if(EFI_ERROR(status)) goto unmount;
    volume->next=volumes; volumes=volume; ++volume_count;
    if(diagnostics) Print(L"[Boot SO] mounted module on read-only child handle\n");
    return EFI_SUCCESS;
unmount:
    (void)volume->image.api->unmount();
failure:
    free_pool(volume->path); free_pool(volume->bounce_allocation);
    BootSoUnload(&volume->image); free_pool(volume); return status;
}
static EFI_STATUS unsigned_modules_allowed(EFI_SYSTEM_TABLE *table) {
    UINT8 secure=1;
    UINTN size=sizeof(secure);
    EFI_STATUS status=table->RuntimeServices->GetVariable(L"SecureBoot",
                                         &global_guid,NULL,&size,&secure);
    if(status==EFI_NOT_FOUND) return EFI_SUCCESS;
    if(EFI_ERROR(status) || size!=1 || secure!=0) return EFI_SECURITY_VIOLATION;
    return EFI_SUCCESS;
}
static BOOLEAN suffix_so(CHAR16 *name) {
    UINTN n=0; while(name[n] && n<255u) ++n;
    return n>=3u && n<255u && name[n-3u]=='.' && name[n-2u]=='s' && name[n-1u]=='o';
}
static EFI_STATUS read_module(EFI_FILE_PROTOCOL *directory, CHAR16 *name, SoModule *module) {
    EFI_FILE_PROTOCOL *file=NULL;
    EFI_STATUS status=directory->Open(directory,&file,name,EFI_FILE_MODE_READ,0);
    if(EFI_ERROR(status)) return status;
    UINT8 info_buffer[1024]; UINTN info_bytes=sizeof(info_buffer);
    status=file->GetInfo(file,&file_info_guid,&info_bytes,info_buffer);
    if(EFI_ERROR(status)) goto done;
    if(info_bytes<SIZE_OF_EFI_FILE_INFO+sizeof(CHAR16)) { status=EFI_VOLUME_CORRUPTED; goto done; }
    UINT64 length=((EFI_FILE_INFO *)info_buffer)->FileSize;
    if(length<64u || length>BOOT_SO_MAX_FILE) { status=EFI_BAD_BUFFER_SIZE; goto done; }
    module->bytes=pool((UINTN)length);
    if(!module->bytes) { status=EFI_OUT_OF_RESOURCES; goto done; }
    module->length=(UINTN)length;
    UINTN position=0;
    while(position<module->length) {
        UINTN got=module->length-position;
        if(got>65536u) got=65536u;
        UINTN requested=got;
        status=file->Read(file,&got,(UINT8 *)module->bytes+position);
        if(EFI_ERROR(status) || !got || got>requested) { status=EFI_DEVICE_ERROR; goto done; }
        position+=got;
    }
    if(BootSoValidate(module->bytes,module->length)) status=EFI_UNSUPPORTED;
done:
    file->Close(file);
    if(EFI_ERROR(status)) { free_pool(module->bytes); clear_bytes(module,sizeof(*module)); }
    return status;
}
UINTN BootSoLoadDirectory(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL *root,
                           CHAR16 *path, BOOLEAN verbose) {
    EFI_LOADED_IMAGE *loaded=NULL;
    EFI_GUID loaded_guid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
    EFI_FILE_PROTOCOL *directory=NULL;
    diagnostics=verbose;
#ifdef __MAKEWITH_GNUEFI
    EFI_SYSTEM_TABLE *table=ST;
#else
    EFI_SYSTEM_TABLE *table=gST;
#endif
    services=table->BootServices;
    if(!root || !path || EFI_ERROR(root->Open(root,&directory,path,EFI_FILE_MODE_READ,0))) return 0;
    if(EFI_ERROR(unsigned_modules_allowed(table))) {
        if(diagnostics) Print(L"[Boot SO] unsigned ELF modules rejected by Secure Boot policy\n");
        directory->Close(directory); return 0;
    }
    (void)services->LocateProtocol(&memory_attributes_guid,NULL,(VOID **)&memory_attributes);
    if(diagnostics) Print(L"[Boot SO] platform memory permissions: %s\n",
                           memory_attributes?L"enforced":L"protocol unavailable");
    if(EFI_ERROR(services->HandleProtocol(image_handle,&loaded_guid,(VOID **)&loaded))) {
        directory->Close(directory); return 0;
    }
    UINT8 buffer[4096];
    UINTN entries=0;
    while(module_count<SO_MAX_MODULES && entries++<256u) {
        UINTN bytes=sizeof(buffer);
        EFI_STATUS status=directory->Read(directory,&bytes,buffer);
        if(EFI_ERROR(status) || !bytes) break;
        EFI_FILE_INFO *info=(EFI_FILE_INFO *)buffer;
        if(bytes<SIZE_OF_EFI_FILE_INFO+sizeof(CHAR16) ||
            bytes>sizeof(buffer) || info->Attribute&EFI_FILE_DIRECTORY) continue;
        UINTN name_chars=(bytes-SIZE_OF_EFI_FILE_INFO)/sizeof(CHAR16), name_end=0;
        while(name_end<name_chars && info->FileName[name_end]) ++name_end;
        if(name_end==name_chars || !suffix_so(info->FileName)) continue;
        SoModule *module=&modules[module_count];
        status=read_module(directory,info->FileName,module);
        if(EFI_ERROR(status)) {
            if(diagnostics) Print(L"[Boot SO] rejected %s: %r\n",info->FileName,status);
            continue;
        }
        /* No module code ran in validation. Each matching volume receives a
         * private instance, so one module's global mount cannot alias another. */
        ++module_count;
        if(diagnostics) Print(L"[Boot SO] validated %s (%d bytes)\n",info->FileName,module->length);
    }
    directory->Close(directory);
    EFI_HANDLE *handles=NULL; UINTN count=0, mounted=0;
    if(EFI_ERROR(services->LocateHandleBuffer(ByProtocol,&block_guid,
                                    NULL,&count,&handles))) return 0;
    for(UINTN i=0;i<count && i<256u && volume_count<SO_MAX_VOLUMES;++i) {
        if(handles[i]==loaded->DeviceHandle) continue; /* retain the bootstrap reader */
        for(UINTN j=0;j<module_count;++j) {
            EFI_STATUS status=bind_module(&modules[j],handles[i]);
            if(!EFI_ERROR(status)) { ++mounted; break; }
        }
    }
    free_pool(handles);
    return mounted;
}
#else
BOOLEAN BootSoOwnsParent(EFI_HANDLE handle) { (void)handle; return FALSE; }
BOOLEAN BootSoHandleMatches(EFI_HANDLE handle, const CHAR16 *id) { (void)handle; (void)id; return FALSE; }
UINTN BootSoLoadDirectory(EFI_HANDLE i, EFI_FILE_PROTOCOL *r, CHAR16 *p, BOOLEAN v) {
    (void)i; (void)r; (void)p; (void)v; return 0;
}
#endif
