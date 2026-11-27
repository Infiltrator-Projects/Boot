/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "common_v2_uefi_trust.h"
#include "common_v2_trust.h"
#include <efilib.h>
#include "../include/boot_call_wrapper.h"

static EFI_GUID global_guid={0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0x00,0xe0,0x98,0x03,0x2b,0x8c}};
static EFI_GUID security_db_guid={0xd719b2cb,0x3d3a,0x4596,{0xa3,0xbc,0xda,0xd0,0x0e,0x67,0x65,0x6f}};
#define TRUST_ATTRIBUTES 0x27U /* NV | BS | RT | TIME_BASED_AUTHENTICATED_WRITE */
#define MAX_DB (4U*1024U*1024U)
static int user_mode(void)
{
    UINT8 setup=1; UINTN n=sizeof(setup); UINT32 attrs=0;
    EFI_STATUS s=boot_call5_wrapper(RT->GetVariable,L"SetupMode",&global_guid,&attrs,&n,&setup);
    return s==EFI_SUCCESS && n==sizeof(setup) && setup==0;
}
static int read_db(CHAR16 *name,VOID **out,UINTN *bytes)
{
    UINTN n=0,capacity; UINT32 attrs=0; VOID *p;
    EFI_STATUS s=boot_call5_wrapper(RT->GetVariable,name,&security_db_guid,&attrs,&n,NULL);
    *out=NULL;*bytes=0;
    /* Missing or unverifiable dbx is deliberately not an implicit allow.
     * The platform administrator must provision both databases. */
    if(s!=EFI_BUFFER_TOO_SMALL || !n || n>MAX_DB) return -1;
    capacity=n;p=AllocatePool(n);
    if(!p) return -1;
    s=boot_call5_wrapper(RT->GetVariable,name,&security_db_guid,&attrs,&n,p);
    if(s!=EFI_SUCCESS || !n || n>capacity || (attrs&TRUST_ATTRIBUTES)!=TRUST_ATTRIBUTES) {
        FreePool(p);return -1;
    }
    *out=p;*bytes=n;return 0;
}
int BootV2Authenticate(VOID *unused,const void *data,size_t bytes)
{
    VOID *db=NULL,*dbx=NULL; UINTN n=0,xn=0; uint8_t digest[32]; int rc=-1;
    (void)unused;
    if(!data || !bytes || bytes>64U*1024U*1024U || !user_mode()) return -1;
    if(read_db(L"db",&db,&n) || read_db(L"dbx",&dbx,&xn)) goto out;
    boot_v2_sha256(data,bytes,digest);
    if(user_mode()) rc=boot_v2_trust_digest(db,n,dbx,xn,digest);
out:
    if(db) FreePool(db);
    if(dbx) FreePool(dbx);
    return rc;
}
