/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common_v2_uefi_memory.h"
#include <efilib.h>
#include "../include/boot_call_wrapper.h"

/* UEFI 2.10 Memory Attribute Protocol. Private names avoid colliding with
 * GNU-EFI versions that already declare the standard protocol. */
typedef struct BootMemoryAttributeProtocol BootMemoryAttributeProtocol;
struct BootMemoryAttributeProtocol {
    EFI_STATUS (EFIAPI *Get)(BootMemoryAttributeProtocol *,EFI_PHYSICAL_ADDRESS,UINT64,UINT64 *);
    EFI_STATUS (EFIAPI *Set)(BootMemoryAttributeProtocol *,EFI_PHYSICAL_ADDRESS,UINT64,UINT64);
    EFI_STATUS (EFIAPI *Clear)(BootMemoryAttributeProtocol *,EFI_PHYSICAL_ADDRESS,UINT64,UINT64);
};
static EFI_GUID memory_guid={0xf4560cf6,0x40ec,0x4b4a,{0xa1,0x92,0xbf,0x1d,0x57,0xd0,0xb1,0x89}};
#define UEFI_RP 0x0000000000002000ULL
#define UEFI_XP 0x0000000000004000ULL
#define UEFI_RO 0x0000000000020000ULL
static UINT64 to_efi(unsigned mask)
{
    return ((mask&BOOT_V2_MEM_RP)?UEFI_RP:0)|((mask&BOOT_V2_MEM_RO)?UEFI_RO:0)|
        ((mask&BOOT_V2_MEM_XP)?UEFI_XP:0);
}
static int status(BootV2UefiMemory *m,EFI_STATUS s)
{ m->last_status=s; return s==EFI_SUCCESS?0:-1; }
static int allocate(void *ctx,size_t n,uintptr_t *p)
{
    BootV2UefiMemory *m=ctx; EFI_PHYSICAL_ADDRESS addr=0;
    EFI_STATUS s=boot_call4_wrapper(BS->AllocatePages,AllocateAnyPages,EfiLoaderData,n/4096,&addr);
    if(status(m,s)) return -1;
    *p=(uintptr_t)addr; return 0;
}
static int free_pages(void *ctx,uintptr_t p,size_t n)
{
    return status(ctx,boot_call2_wrapper(BS->FreePages,p,n/4096));
}
static int set_attrs(void *ctx,uintptr_t p,size_t n,unsigned mask)
{
    BootV2UefiMemory *m=ctx; BootMemoryAttributeProtocol *a=m->protocol;
    return status(m,boot_call4_wrapper(a->Set,a,p,n,to_efi(mask)));
}
static int clear_attrs(void *ctx,uintptr_t p,size_t n,unsigned mask)
{
    BootV2UefiMemory *m=ctx; BootMemoryAttributeProtocol *a=m->protocol;
    return status(m,boot_call4_wrapper(a->Clear,a,p,n,to_efi(mask)));
}
static int get_attrs(void *ctx,uintptr_t p,size_t n,unsigned *mask)
{
    BootV2UefiMemory *m=ctx; BootMemoryAttributeProtocol *a=m->protocol; UINT64 flags=0;
    if(status(m,boot_call4_wrapper(a->Get,a,p,n,&flags))) return -1;
    *mask=((flags&UEFI_RP)?BOOT_V2_MEM_RP:0)|((flags&UEFI_RO)?BOOT_V2_MEM_RO:0)|
        ((flags&UEFI_XP)?BOOT_V2_MEM_XP:0);
    return 0;
}
EFI_STATUS BootV2UefiMemoryInit(BootV2UefiMemory *m,void *trust,
                              int (*auth)(void *,const void *,size_t))
{
    BootMemoryAttributeProtocol *a=NULL;
    BootV2MemoryBackend b={0}; EFI_STATUS s;
    if(!m || !auth || m->memory.allocated || m->memory.quarantined) return EFI_INVALID_PARAMETER;
    s=boot_call3_wrapper(BS->LocateProtocol,&memory_guid,NULL,(VOID **)&a);
    if(status(m,s)) return s;
    if(!a || !a->Get || !a->Set || !a->Clear) return EFI_UNSUPPORTED;
    m->protocol=a;
    b.context=m; b.allocate=allocate; b.free=free_pages;
    b.set=set_attrs; b.clear=clear_attrs; b.get=get_attrs;
    return boot_v2_memory_host(&m->memory,&b,trust,auth,&m->host)?EFI_UNSUPPORTED:EFI_SUCCESS;
}
