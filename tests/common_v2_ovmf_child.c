/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A real signed EFI application launched by BOOT from the independent
 * filesystem volume. Read proof through its actual EFI SFS protocol and
 * leave a disposable firmware variable only on exact byte agreement. */
#include <efi.h>
#include <efilib.h>
static EFI_GUID proof_guid={0xd822c2b1,0x26aa,0x44a4,{0x9b,0x47,0x40,0x9c,0xa8,0x37,0x11,0x8f}};
EFI_STATUS efi_main(EFI_HANDLE image,EFI_SYSTEM_TABLE *system)
{
    EFI_LOADED_IMAGE *li=NULL;EFI_FILE_PROTOCOL *root=NULL,*file=NULL;
    static CHAR8 expected[]="Common V2 native media proof\n";
    static CHAR8 verdict[]="BOOT_V2_CHILD_READ_AND_RETURN_PASS";
    CHAR8 buf[64];UINTN n=sizeof(buf);EFI_STATUS s;
    InitializeLib(image,system);
    s=uefi_call_wrapper(BS->HandleProtocol,3,image,&LoadedImageProtocol,(VOID **)&li);
    if(s!=EFI_SUCCESS||!li||!li->DeviceHandle)return EFI_LOAD_ERROR;
    root=LibOpenRoot(li->DeviceHandle);if(!root)return EFI_NOT_FOUND;
    s=uefi_call_wrapper(root->Open,5,root,&file,L"\\proof.txt",EFI_FILE_MODE_READ,0);
    if(s==EFI_SUCCESS){
        s=uefi_call_wrapper(file->Read,3,file,&n,buf);
        uefi_call_wrapper(file->Close,1,file);
    }
    uefi_call_wrapper(root->Close,1,root);
    if(s!=EFI_SUCCESS||n!=sizeof(expected)-1||CompareMem(buf,expected,n))return EFI_VOLUME_CORRUPTED;
    return uefi_call_wrapper(RT->SetVariable,5,L"BootV2TestResult",&proof_guid,7,sizeof(verdict),verdict);
}
