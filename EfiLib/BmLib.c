/*
 * EFI library convenience routines for protocol lookup, file metadata and
 * pool reallocation. Callers must respect differences between an EFI handle,
 * a protocol instance and an allocated copy of firmware-managed structures.
 */
 













#ifdef __MAKEWITH_TIANO
#include "Platform.h"
#else
#include "gnuefi-helper.h"
#endif
#include "../include/refit_call_wrapper.h"

 













EFI_STATUS
/*
 * Locates a firmware protocol by GUID; absence is a supported runtime
 * condition and must not be converted into an assumed usable interface.
 */
EfiLibLocateProtocol (
  IN  EFI_GUID    *ProtocolGuid,
  OUT VOID        **Interface
  )
{
  EFI_STATUS  Status;

  Status = refit_call3_wrapper(gBS->LocateProtocol,
                  ProtocolGuid,
                  NULL,
                  (VOID **) Interface
                  );
  return Status;
}

 








EFI_FILE_HANDLE
EfiLibOpenRoot (
  IN EFI_HANDLE                   DeviceHandle
  )
{
  EFI_STATUS                      Status;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Volume;
  EFI_FILE_HANDLE                 File;

  File = NULL;

   
   
   
  Status = refit_call3_wrapper(gBS->HandleProtocol,
                  DeviceHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **) &Volume
                  );

   
   
   
  if (!EFI_ERROR (Status)) {
    Status = Volume->OpenVolume (
                      Volume,
                      &File
                      );
  }
   
   
   
  return EFI_ERROR (Status) ? NULL : File;
}

 








CHAR16 *
EfiStrDuplicate (
  IN CHAR16   *Src
				 )
{
  CHAR16  *Dest;
  UINTN   Size;

  Size  = StrSize (Src);  
  Dest  = AllocateZeroPool (Size);
  if (Dest != NULL) {
    CopyMem (Dest, Src, Size);
  }

  return Dest;
}

 









EFI_FILE_INFO *
EfiLibFileInfo (
  IN EFI_FILE_HANDLE      FHand
  )
{
  EFI_STATUS    Status;
  EFI_FILE_INFO *FileInfo = NULL;
  UINTN         Size = 0;
  
  Status = FHand->GetInfo (FHand, &gEfiFileInfoGuid, &Size, FileInfo);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    FileInfo = AllocateZeroPool (Size);
    Status = FHand->GetInfo (FHand, &gEfiFileInfoGuid, &Size, FileInfo);
  }
  
  return EFI_ERROR(Status)?NULL:FileInfo;
}

EFI_FILE_SYSTEM_INFO *
EfiLibFileSystemInfo (
                IN EFI_FILE_HANDLE      FHand
                )
{
  EFI_STATUS    Status;
  EFI_FILE_SYSTEM_INFO *FileSystemInfo = NULL;
  UINTN         Size = 0;

  Status = FHand->GetInfo (FHand, &gEfiFileSystemInfoGuid, &Size, FileSystemInfo);
  if (Status == EFI_BUFFER_TOO_SMALL) {
    FileSystemInfo = AllocateZeroPool (Size);
    Status = FHand->GetInfo (FHand, &gEfiFileSystemInfoGuid, &Size, FileSystemInfo);
  }

  return EFI_ERROR(Status)?NULL:FileSystemInfo;
}

 











VOID *
/*
 * Replaces a pool-backed buffer while preserving intended bytes; callers
 * must not keep using the old pointer after successful reallocation.
 */
EfiReallocatePool (
  IN VOID                 *OldPool,
  IN UINTN                OldSize,
  IN UINTN                NewSize
  )
{
  VOID  *NewPool;

  NewPool = NULL;
  if (NewSize != 0) {
    NewPool = AllocateZeroPool (NewSize);
  }

  if (OldPool != NULL) {
    if (NewPool != NULL) {
      CopyMem (NewPool, OldPool, OldSize < NewSize ? OldSize : NewSize);
    }

    FreePool (OldPool);
  }

  return NewPool;
}
