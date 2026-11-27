/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Disposable OVMF fixture only. Writes ephemeral authenticated platform
 * variables on an isolated VM, never on a host or production machine.
 * BOOT itself contains no provisioning or trust bypass. */
#include <efi.h>
#include <efilib.h>
static EFI_GUID global={0x8be4df61,0x93ca,0x11d2,{0xaa,0x0d,0x00,0xe0,0x98,0x03,0x2b,0x8c}};
static EFI_GUID dbguid={0xd719b2cb,0x3d3a,0x4596,{0xa3,0xbc,0xda,0xd0,0x0e,0x67,0x65,0x6f}};
static EFI_STATUS install(EFI_FILE_PROTOCOL *root,CHAR16 *file,CHAR16 *name,EFI_GUID *guid)
{
    EFI_FILE_PROTOCOL *f=NULL;EFI_STATUS s;UINTN n=65536;VOID *buf=AllocatePool(n);
    if(!buf)return EFI_OUT_OF_RESOURCES;
    s=uefi_call_wrapper(root->Open,5,root,&f,file,EFI_FILE_MODE_READ,0);
    if(s==EFI_SUCCESS){
        s=uefi_call_wrapper(f->Read,3,f,&n,buf);
        uefi_call_wrapper(f->Close,1,f);
        if(s==EFI_SUCCESS&&n&&n<65536)s=uefi_call_wrapper(RT->SetVariable,5,name,guid,0x27,n,buf);
        else if(s==EFI_SUCCESS)s=EFI_BAD_BUFFER_SIZE;
    }
    FreePool(buf);Print(L"V2 test provision %s: %r\r\n",name,s);return s;
}
EFI_STATUS efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *system)
{
    EFI_LOADED_IMAGE *li=NULL;EFI_FILE_PROTOCOL *root;
    EFI_STATUS s;UINT8 setup=1,secure=0;UINTN n=1;
    EFI_HANDLE child=NULL;EFI_DEVICE_PATH *path;
    InitializeLib(image,system);
    s=uefi_call_wrapper(BS->HandleProtocol,3,image,&LoadedImageProtocol,(VOID **)&li);
    if(s!=EFI_SUCCESS||!li)return EFI_LOAD_ERROR;
    s=uefi_call_wrapper(RT->GetVariable,5,L"SetupMode",&global,NULL,&n,&setup);
    if(s!=EFI_SUCCESS||n!=1)return EFI_SECURITY_VIOLATION;
    if(setup){
        root=LibOpenRoot(li->DeviceHandle);if(!root)return EFI_NOT_FOUND;
        s=install(root,L"\\db.auth",L"db",&dbguid);
        if(s==EFI_SUCCESS)s=install(root,L"\\dbx.auth",L"dbx",&dbguid);
        if(s==EFI_SUCCESS)s=install(root,L"\\KEK.auth",L"KEK",&global);
        if(s==EFI_SUCCESS)s=install(root,L"\\PK.auth",L"PK",&global);
        uefi_call_wrapper(root->Close,1,root);
        if(s!=EFI_SUCCESS)return s;
        Print(L"V2 test keys enrolled, rebooting\r\n");
        uefi_call_wrapper(RT->ResetSystem,4,EfiResetCold,EFI_SUCCESS,0,NULL);
        return EFI_DEVICE_ERROR;
    }
    n=1;s=uefi_call_wrapper(RT->GetVariable,5,L"SecureBoot",&global,NULL,&n,&secure);
    if(s!=EFI_SUCCESS||n!=1||secure!=1){Print(L"V2 test Secure Boot not enabled\r\n");return EFI_SECURITY_VIOLATION;}
    path=FileDevicePath(li->DeviceHandle,L"\\EFI\\InfiltratorOS\\BOOT\\BOOTX64.EFI");
    if(!path)return EFI_OUT_OF_RESOURCES;
    s=uefi_call_wrapper(BS->LoadImage,6,FALSE,image,path,NULL,0,&child);
    FreePool(path);
    if(s==EFI_SUCCESS)s=uefi_call_wrapper(BS->StartImage,3,child,NULL,NULL);
    Print(L"V2 BOOT returned: %r\r\n",s);return s;
}
