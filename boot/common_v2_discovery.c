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
BootV2Provider *BootV2Providers(VOID) {return providers;}
const BootV2Provider *BootV2DiscoverProviders(VOID)
{
    BOOT_DIR_ITER it;EFI_FILE_INFO *entry;
    UINTN accepted=0,candidates=0;
    if(scanned) return providers;
    scanned=TRUE;
    if(!SelfDir) return NULL;
    DirIterOpen(SelfDir,L"drivers_x64",&it);
    while(accepted<MAX_MODULES && candidates<MAX_CANDIDATES &&
          DirIterNext(&it,2,NULL,&entry)) {
        BootV2Provider *p,**at;char id[IFS_FS_MAX_FORMAT_ID+1];
        UINTN n;VOID *data;
        ++candidates;
        if(!format_id(entry->FileName,id)) continue;
        for(at=&providers;*at && compare((*at)->id,id)<0;at=&(*at)->next){}
        if(*at && !compare((*at)->id,id)) continue;
        p=AllocateZeroPool(sizeof(*p));
        if(!p) break;
        for(UINTN i=0;id[i];i++) p->id[i]=id[i];
        if(BootV2UefiMemoryInit(&p->memory,NULL,BootV2Authenticate)!=EFI_SUCCESS) {
            FreePool(p);break; /* same firmware protocol for all providers */
        }
        data=read_module(it.DirHandle,entry,&n);
        if(!data){FreePool(p);continue;}
        if(boot_v2_elf_load(&p->memory.host,data,n,&p->image) ||
           p->image.bytes>MAX_MAPPED_BYTES-mapped_bytes ||
           boot_v2_module_inspect(&p->image,p->id,&p->module)!=IFS_FS_OK ||
           !boot_v2_module_readable_core(&p->module)) {
            FreePool(data);
            boot_v2_elf_unload(&p->memory.host,&p->image);
            LOG(1,LOG_LINE_NORMAL,L"Common V2 module rejected: %s",entry->FileName);
            if(p->memory.memory.quarantined) {FreePool(p);break;}
            FreePool(p);continue;
        }
        FreePool(data);
        mapped_bytes+=p->image.bytes;
        p->next=*at;*at=p;++accepted;
        LOG(1,LOG_LINE_NORMAL,L"Common V2 authenticated reader: %s",entry->FileName);
        /* DirIter owns entry, so the next iteration or Close releases it. */
    }
    DirIterClose(&it);
    return providers;
}
VOID BootV2ReleaseProviders(VOID)
{
    BootV2Provider **at=&providers;
    while(*at) {
        BootV2Provider *p=*at;
        if(p->mounted){at=&p->next;continue;}
        mapped_bytes-=p->image.bytes;
        boot_v2_elf_unload(&p->memory.host,&p->image);
        *at=p->next;FreePool(p);
    }
}
