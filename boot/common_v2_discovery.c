/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common_v2_discovery.h"
#include "common_v2_uefi_trust.h"
#include "lib.h"
#include "log.h"
#include "../include/boot_call_wrapper.h"

#define MAX_MODULE_BYTES (64U*1024U*1024U)
#define MAX_MAPPED_BYTES (128U*1024U*1024U)
#define MAX_MODULES 64U
#define MAX_CANDIDATES 256U
static BootV2Provider *providers;
static BOOLEAN scanned;
static BOOLEAN quarantined;
static UINTN mapped_bytes;
static int compare(const char *a,const char *b)
{
    while(*a && *a==*b){++a;++b;}
    return (unsigned char)*a-(unsigned char)*b;
}
static CHAR16 lower(CHAR16 c)
{return c>=L'A' && c<=L'Z'?c+(L'a'-L'A'):c;}
static int format_id(const CHAR16 *name,char id[IFS_FS_MAX_FORMAT_ID+1])
{
    UINTN n=StrLen((CHAR16 *)name),i;
    if(n<4 || n>IFS_FS_MAX_FORMAT_ID+3 || name[n-3]!=L'.' ||
       lower(name[n-2])!=L's' || lower(name[n-1])!=L'o') return 0;
    for(i=0;i<n-3;i++) {
        CHAR16 c=lower(name[i]);
        if(c>127) return 0;
        id[i]=(char)c;
    }
    id[i]=0;return ifs_fs_format_id_valid(id);
}
static VOID *read_module(EFI_FILE_PROTOCOL *dir,EFI_FILE_INFO *entry,UINTN *size)
{
    EFI_FILE_PROTOCOL *file=NULL;VOID *data=NULL;
    UINTN n,extra=1;UINT8 tail;EFI_STATUS s;
    *size=0;
    if(!entry->FileSize || entry->FileSize>MAX_MODULE_BYTES) return NULL;
    n=(UINTN)entry->FileSize;
    s=boot_call5_wrapper(dir->Open,dir,&file,entry->FileName,EFI_FILE_MODE_READ,0);
    if(s!=EFI_SUCCESS || !file) return NULL;
    data=AllocatePool(n);
    if(!data) goto close;
    s=boot_call3_wrapper(file->Read,file,&n,data);
    if(s!=EFI_SUCCESS || n!=entry->FileSize ||
       boot_call3_wrapper(file->Read,file,&extra,&tail)!=EFI_SUCCESS || extra) {
        FreePool(data);data=NULL;goto close;
    }
    *size=n;
close:
    boot_call1_wrapper(file->Close,file);
    return data;
}
/* Discovery decisions are distinct from provider ownership. Only a
 * ready or quarantined result transfers an allocated provider to the list. */
typedef enum {
    PROVIDER_SKIP,
    PROVIDER_STOP,
    PROVIDER_READY,
    PROVIDER_QUARANTINED
} ProviderLoadResult;

/* Load and validate one candidate without editing the discovery list.
 * The caller owns list ordering and the total mapped-byte accounting. */
static ProviderLoadResult load_provider(EFI_FILE_PROTOCOL *dir,EFI_FILE_INFO *entry,
                                        const char *id,UINTN available_bytes,
                                        BootV2Provider **out)
{
    BootV2Provider *p;
    UINTN n;
    VOID *data;
    EFI_STATUS memstatus;
    int mapped,inspected;

    *out=NULL;
    p=AllocateZeroPool(sizeof(*p));
    if(!p)return PROVIDER_STOP;
    for(UINTN i=0;id[i];i++)p->id[i]=id[i];

    memstatus=BootV2UefiMemoryInit(&p->memory,NULL,BootV2Authenticate);
    if(memstatus!=EFI_SUCCESS){
        LOG(1,LOG_LINE_NORMAL,L"Common V2 unavailable: firmware memory attribute protocol status %r",memstatus);
        FreePool(p);
        return PROVIDER_STOP; /* Same firmware protocol for every provider. */
    }

    data=read_module(dir,entry,&n);
    if(!data){
        LOG(1,LOG_LINE_NORMAL,L"Common V2 cannot read: %s",entry->FileName);
        FreePool(p);
        return PROVIDER_SKIP;
    }

    mapped=boot_v2_elf_load(&p->memory.host,data,n,&p->image);
    inspected=(mapped||p->image.bytes>available_bytes)?
        IFS_FS_INVALID:boot_v2_module_inspect(&p->image,p->id,&p->module);
    FreePool(data);

    if(!mapped&&p->image.bytes<=available_bytes&&inspected==IFS_FS_OK&&
       boot_v2_module_readable_core(&p->module)){
        *out=p;
        return PROVIDER_READY;
    }

    LOG(1,LOG_LINE_NORMAL,L"Common V2 validation detail: map=%d descriptor=%d memory=%r",mapped,inspected,p->memory.last_status);
    boot_v2_elf_unload(&p->memory.host,&p->image);
    LOG(1,LOG_LINE_NORMAL,L"Common V2 module rejected: %s",entry->FileName);

    if(p->memory.memory.quarantined){
        /* Preserve the ownership record until reset: the mapping cannot
         * safely be destroyed or counted as an authenticated reader. */
        *out=p;
        return PROVIDER_QUARANTINED;
    }
    FreePool(p);
    return PROVIDER_SKIP;
}

/* Return the insertion link for an ID, retaining deterministic ordering. */
static BootV2Provider **provider_slot(const char *id)
{
    BootV2Provider **at=&providers;
    while(*at&&compare((*at)->id,id)<0)at=&(*at)->next;
    return at;
}

BootV2Provider *BootV2Providers(VOID) {return providers;}

const BootV2Provider *BootV2DiscoverProviders(VOID)
{
    BOOT_DIR_ITER it;
    EFI_FILE_INFO *entry;
    UINTN accepted=0,candidates=0;
    if(scanned)return providers;
    scanned=TRUE;
    if(!SelfDir)return NULL;

    DirIterOpen(SelfDir,L"drivers_x64",&it);
    while(accepted<MAX_MODULES&&candidates<MAX_CANDIDATES&&
          DirIterNext(&it,2,NULL,&entry)){
        BootV2Provider *p,**at;
        char id[IFS_FS_MAX_FORMAT_ID+1];
        ProviderLoadResult decision;

        ++candidates;
        if(!format_id(entry->FileName,id))continue;
        at=provider_slot(id);
        if(*at&&!compare((*at)->id,id))continue;

        decision=load_provider(it.DirHandle,entry,id,MAX_MAPPED_BYTES-mapped_bytes,&p);
        if(decision==PROVIDER_SKIP)continue;
        if(decision==PROVIDER_STOP)break;

        /* Both retained outcomes carry an ownership record. Only a ready
         * provider contributes to the usable count and mapped-byte budget. */
        p->next=*at;
        *at=p;
        if(decision==PROVIDER_QUARANTINED){
            quarantined=TRUE;
            break;
        }
        mapped_bytes+=p->image.bytes;
        ++accepted;
        LOG(1,LOG_LINE_NORMAL,L"Common V2 authenticated reader: %s",entry->FileName);
        /* DirIter owns entry, so the next iteration or Close releases it. */
    }
    DirIterClose(&it);
    return providers;
}
BOOLEAN BootV2HasProviders(VOID) {return providers!=NULL || quarantined;}
EFI_STATUS BootV2ReleaseProviders(VOID)
{
    BootV2Provider **at=&providers;
    EFI_STATUS rc=EFI_SUCCESS;
    while(*at) {
        BootV2Provider *p=*at;
        if(p->mounted || p->memory.memory.quarantined){rc=EFI_ACCESS_DENIED;at=&p->next;continue;}
        mapped_bytes-=p->image.bytes;
        boot_v2_elf_unload(&p->memory.host,&p->image);
        if(p->memory.memory.quarantined){quarantined=TRUE;rc=EFI_DEVICE_ERROR;at=&p->next;continue;}
        *at=p->next;FreePool(p);
    }
    return quarantined?EFI_DEVICE_ERROR:rc;
}
