/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common_v2_uefi_storage.h"
#include <efilib.h>
#include "global.h"
#include "log.h"
#include "../include/boot_call_wrapper.h"

static EFI_GUID block_guid={0x964e5b21,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_GUID disk_guid={0xce345171,0xba0b,0x11d2,{0x8e,0x4f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_GUID sfs_guid={0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_GUID path_guid={0x09576e91,0x6d3f,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

/* UEFI Media Access, Block I/O: revision-2 physical geometry fields are
 * zero on logical partition handles. Resolve the exact parent device path
 * by removing only the last Hard Drive Media Device Path node. Never
 * substitute a guessed sector size, a different disk or a partial match. */
static int parent_geometry(BootV2UefiStorage *v)
{
    EFI_DEVICE_PATH *path=NULL,*cursor,*remaining;
    UINT8 prefix[4096];
    UINTN at=0,last=0,len;
    HARDDRIVE_DEVICE_PATH hd;
    EFI_BLOCK_IO *p=NULL;
    EFI_HANDLE parent=NULL;
    EFI_STATUS s;
    if(boot_call3_wrapper(BS->HandleProtocol,v->handle,&path_guid,(VOID **)&path)!=EFI_SUCCESS||!path)return -1;
    cursor=path;
    for(;;) {
        len=DevicePathNodeLength(cursor);
        if(len<sizeof(EFI_DEVICE_PATH)||len>sizeof(prefix)-at)return -1;
        if(DevicePathType(cursor)==END_DEVICE_PATH_TYPE) {
            if(DevicePathSubType(cursor)!=END_ENTIRE_DEVICE_PATH_SUBTYPE||len!=sizeof(EFI_DEVICE_PATH)||!at)return -1;
            break;
        }
        last=at;at+=len;cursor=NextDevicePathNode(cursor);
        if(at>sizeof(prefix)-sizeof(EFI_DEVICE_PATH))return -1;
    }
    cursor=(EFI_DEVICE_PATH *)((UINT8 *)path+last);
    LOG(1,LOG_LINE_NORMAL,L"Common V2 parent: terminal node type=%d subtype=%d bytes=%d struct=%d",DevicePathType(cursor),DevicePathSubType(cursor),DevicePathNodeLength(cursor),sizeof(hd));
    /* The wire format is exactly 42 bytes. Some GNU-EFI headers expose an
     * unpacked struct with tail padding, which must never be read from the
     * device path or used as the on-wire length. Decode little-endian
     * partition start and length directly from the standard byte offsets. */
    if(DevicePathType(cursor)!=MEDIA_DEVICE_PATH||DevicePathSubType(cursor)!=MEDIA_HARDDRIVE_DP||
       DevicePathNodeLength(cursor)!=42)return -1;
    SetMem(&hd,sizeof(hd),0);
    for(unsigned i=0;i<8;i++){
        hd.PartitionStart|=(UINT64)((UINT8 *)cursor)[8+i]<<(8*i);
        hd.PartitionSize|=(UINT64)((UINT8 *)cursor)[16+i]<<(8*i);
    }
    if(!hd.PartitionSize||hd.PartitionSize-1!=v->block->Media->LastBlock)return -1;
    CopyMem(prefix,path,last);
    prefix[last]=END_DEVICE_PATH_TYPE;prefix[last+1]=END_ENTIRE_DEVICE_PATH_SUBTYPE;
    prefix[last+2]=sizeof(EFI_DEVICE_PATH);prefix[last+3]=0;
    remaining=(EFI_DEVICE_PATH *)prefix;
    s=boot_call3_wrapper(BS->LocateDevicePath,&block_guid,&remaining,&parent);
    LOG(1,LOG_LINE_NORMAL,L"Common V2 parent: LocateDevicePath=%r exact=%d parent=%lx",s,remaining==(EFI_DEVICE_PATH *)(prefix+last),(UINT64)(UINTN)parent);
    if(s!=EFI_SUCCESS||!parent||parent==v->handle||
       remaining!=(EFI_DEVICE_PATH *)(prefix+last))return -1;
    if(boot_call3_wrapper(BS->HandleProtocol,parent,&block_guid,(VOID **)&p)!=EFI_SUCCESS||!p||!p->Media)return -1;
    LOG(1,LOG_LINE_NORMAL,L"Common V2 parent: rev=%lx sector=%d ratio=%d last=%lx partition start=%lx size=%lx",p->Revision,p->Media->BlockSize,p->Revision>=0x00020001ULL?p->Media->LogicalBlocksPerPhysicalBlock:0,p->Media->LastBlock,hd.PartitionStart,hd.PartitionSize);
    if(!p->Media->MediaPresent||p->Media->LogicalPartition||
       p->Revision<0x00020001ULL||p->Media->BlockSize!=v->block->Media->BlockSize||
       !p->Media->LogicalBlocksPerPhysicalBlock||
       hd.PartitionStart>p->Media->LastBlock||
       hd.PartitionSize-1>p->Media->LastBlock-hd.PartitionStart)return -1;
    v->parent_handle=parent;v->parent=p;v->parent_media_id=p->Media->MediaId;
    v->parent_last_block=p->Media->LastBlock;
    v->partition_start=hd.PartitionStart;v->partition_blocks=hd.PartitionSize;
    return 0;
}

/* Revision 2 introduces physical geometry. A zero or missing geometry is
 * unknown, not evidence that physical and logical sectors are identical. */
static int geometry(void *ctx, BootV2Geometry *out)
{
    BootV2UefiStorage *v=ctx;
    EFI_BLOCK_IO *block=NULL;
    EFI_DISK_IO *disk=NULL;
    EFI_BLOCK_IO_MEDIA *m;
    UINT32 ratio;
    EFI_BLOCK_IO *parent=NULL;
    if (!v || !out ||
        EFI_ERROR(boot_call3_wrapper(BS->HandleProtocol,v->handle,&block_guid,(VOID **)&block)) ||
        EFI_ERROR(boot_call3_wrapper(BS->HandleProtocol,v->handle,&disk_guid,(VOID **)&disk)) ||
        block!=v->block || disk!=v->disk || !block || !disk || !block->Media) return -1;
    m=block->Media;
    if(m->LogicalPartition) {
        if(!v->parent_handle||!v->parent||
           boot_call3_wrapper(BS->HandleProtocol,v->parent_handle,&block_guid,(VOID **)&parent)!=EFI_SUCCESS||
           parent!=v->parent||!parent->Media||!parent->Media->MediaPresent||
           parent->Media->LogicalPartition||parent->Revision<0x00020001ULL||
           parent->Media->MediaId!=v->parent_media_id||parent->Media->LastBlock!=v->parent_last_block||
           parent->Media->BlockSize!=m->BlockSize||
           !v->partition_blocks||v->partition_blocks-1!=m->LastBlock) return -1;
        ratio=parent->Media->LogicalBlocksPerPhysicalBlock;
    } else {
        if(v->parent||block->Revision<0x00020001ULL)return -1;
        ratio=m->LogicalBlocksPerPhysicalBlock;
    }
    if (!m->BlockSize || !ratio || ratio>65536U/m->BlockSize) return -1;
    *out=(BootV2Geometry){m->LastBlock,m->MediaId,m->BlockSize,m->BlockSize*ratio,
                         !!m->MediaPresent,!!m->ReadOnly};
    return 0;
}
static int read_disk(void *ctx, uint32_t id, uint64_t off, void *buf, uint32_t bytes)
{
    BootV2UefiStorage *v=ctx;
    /* Disk I/O, unlike Block I/O, accepts arbitrary buffer alignment. The
     * storage core still requires sector-aligned offsets and transfer sizes.
     * Firmware independently validates the snapshotted media identifier. */
    return EFI_ERROR(boot_call5_wrapper(v->disk->ReadDisk,v->disk,id,off,bytes,buf)) ? -1 : 0;
}
static void *allocate(void *ctx, size_t bytes)
{ (void)ctx; return AllocatePool(bytes); }
static void release(void *ctx, void *p)
{ (void)ctx; FreePool(p); }

EFI_STATUS BootV2UefiStorageInit(BootV2UefiStorage *v, EFI_HANDLE handle)
{
    EFI_STATUS status;
    VOID *existing=NULL;
    BootV2Backend backend={0};
    if (!v || !handle || v->storage.active) return EFI_INVALID_PARAMETER;
    status=boot_call3_wrapper(BS->HandleProtocol,handle,&sfs_guid,&existing);
    if (!EFI_ERROR(status)) return EFI_ALREADY_STARTED;
    if (status!=EFI_UNSUPPORTED && status!=EFI_NOT_FOUND) return status;
    v->handle=handle;
    status=boot_call3_wrapper(BS->HandleProtocol,handle,&block_guid,(VOID **)&v->block);
    if (EFI_ERROR(status)) return status;
    status=boot_call3_wrapper(BS->HandleProtocol,handle,&disk_guid,(VOID **)&v->disk);
    if (EFI_ERROR(status)) return status;
    if (!v->block || !v->block->Media || !v->disk || !v->disk->ReadDisk) return EFI_UNSUPPORTED;
    LOG(1,LOG_LINE_NORMAL,L"Common V2 BlockIO: handle=%lx revision=%lx logical=%d sector=%d last=%lx",(UINT64)(UINTN)handle,v->block->Revision,v->block->Media->LogicalPartition,v->block->Media->BlockSize,v->block->Media->LastBlock);
    if(v->block->Media->LogicalPartition&&parent_geometry(v)){
        LOG(1,LOG_LINE_NORMAL,L"Common V2 storage: exact parent geometry unavailable");
        return EFI_UNSUPPORTED;
    }
    backend.context=v; backend.geometry=geometry; backend.read=read_disk;
    backend.allocate=allocate; backend.release=release;
    /* No firmware exclusivity or durability claim has been established.
     * Never expose write/flush callbacks until a genuine lease exists. */
    if (boot_v2_storage_init(&v->storage,&backend,1024U*1024U,0)) return EFI_UNSUPPORTED;
    if (boot_v2_host_init(&v->host,&v->storage)!=IFS_FS_OK) {
        boot_v2_storage_close(&v->storage); return EFI_UNSUPPORTED;
    }
    return EFI_SUCCESS;
}
VOID BootV2UefiStorageClose(BootV2UefiStorage *v)
{
    if (v) boot_v2_storage_close(&v->storage);
}
