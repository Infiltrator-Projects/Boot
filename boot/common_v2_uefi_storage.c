/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common_v2_uefi_storage.h"
#include <efilib.h>
#include "../include/boot_call_wrapper.h"

static EFI_GUID block_guid={0x964e5b21,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_GUID disk_guid={0xce345171,0xba0b,0x11d2,{0x8e,0x4f,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
static EFI_GUID sfs_guid={0x964e5b22,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

/* Revision 2 introduces physical geometry. A zero or missing geometry is
 * unknown, not evidence that physical and logical sectors are identical. */
static int geometry(void *ctx, BootV2Geometry *out)
{
    BootV2UefiStorage *v=ctx;
    EFI_BLOCK_IO *block=NULL;
    EFI_DISK_IO *disk=NULL;
    EFI_BLOCK_IO_MEDIA *m;
    UINT32 ratio;
    if (!v || !out ||
        EFI_ERROR(boot_call3_wrapper(BS->HandleProtocol,v->handle,&block_guid,(VOID **)&block)) ||
        EFI_ERROR(boot_call3_wrapper(BS->HandleProtocol,v->handle,&disk_guid,(VOID **)&disk)) ||
        block!=v->block || disk!=v->disk || !block || !disk || !block->Media ||
        block->Revision<0x00020001ULL) return -1;
    m=block->Media;
    ratio=m->LogicalBlocksPerPhysicalBlock;
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
    if (!v->block || !v->disk || !v->disk->ReadDisk) return EFI_UNSUPPORTED;
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
