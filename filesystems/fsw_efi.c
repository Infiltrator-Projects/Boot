/*
 * Bridge from filesystem-neutral read callbacks to EFI Block I/O, Disk I/O,
 * Driver Binding and Simple File System protocol. The driver exposes a new
 * firmware filesystem interface only after mount succeeds. Protocol installs,
 * controller claims, caches and file handles have matching teardown duties.
 */
 




 






























 



#include "fsw_efi.h"
#include "fsw_core.h"
#ifdef __MAKEWITH_GNUEFI
#include "edk2/DriverBinding.h"
#include "edk2/ComponentName.h"
#define gMyEfiSimpleFileSystemProtocolGuid FileSystemProtocol
#else
#define REFIND_EFI_DRIVER_BINDING_PROTOCOL EFI_DRIVER_BINDING_PROTOCOL
#define REFIND_EFI_COMPONENT_NAME_PROTOCOL EFI_COMPONENT_NAME_PROTOCOL
#define REFIND_EFI_COMPONENT_NAME_PROTOCOL_GUID EFI_COMPONENT_NAME_PROTOCOL_GUID
#define REFIND_EFI_DRIVER_BINDING_PROTOCOL_GUID EFI_DRIVER_BINDING_PROTOCOL_GUID
#define REFIND_EFI_DEVICE_PATH_PROTOCOL EFI_DEVICE_PATH_PROTOCOL
#define EFI_FILE_SYSTEM_VOLUME_LABEL_INFO_ID    \
    { 0xDB47D7D3,0xFE81, 0x11d3, {0x9A, 0x35, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D} }
#define gMyEfiSimpleFileSystemProtocolGuid gEfiSimpleFileSystemProtocolGuid
#endif

#include "../include/version.h"

#define DEBUG_LEVEL 0

#ifndef FSTYPE
 
#define FSTYPE ext2
#endif

EFI_GUID gMyEfiDriverBindingProtocolGuid = REFIND_EFI_DRIVER_BINDING_PROTOCOL_GUID;
EFI_GUID gMyEfiComponentNameProtocolGuid = REFIND_EFI_COMPONENT_NAME_PROTOCOL_GUID;
EFI_GUID gMyEfiDiskIoProtocolGuid = REFIND_EFI_DISK_IO_PROTOCOL_GUID;
EFI_GUID gMyEfiBlockIoProtocolGuid = REFIND_EFI_BLOCK_IO_PROTOCOL_GUID;
EFI_GUID gMyEfiFileInfoGuid = EFI_FILE_INFO_ID;
EFI_GUID gMyEfiFileSystemInfoGuid = EFI_FILE_SYSTEM_INFO_ID;
EFI_GUID gMyEfiFileSystemVolumeLabelInfoIdGuid = EFI_FILE_SYSTEM_VOLUME_LABEL_INFO_ID;

 
#define FSW_EFI_STRINGIFY(x) #x
 
#define FSW_EFI_DRIVER_NAME(t) L"BOOT " FSW_EFI_STRINGIFY(t) L" File System Driver"

 

EFI_STATUS EFIAPI fsw_efi_DriverBinding_Supported(IN REFIND_EFI_DRIVER_BINDING_PROTOCOL  *This,
                                                  IN EFI_HANDLE                          ControllerHandle,
                                                  IN REFIND_EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath);
/*
 * UEFI driver-binding start operation for a filesystem-backed controller.
 * This is a firmware protocol integration layer: the mounted FSW volume
 * becomes visible through EFI Simple File System only when registration
 * completes, and teardown must reverse controller claims and allocations.
 */
EFI_STATUS EFIAPI fsw_efi_DriverBinding_Start(IN REFIND_EFI_DRIVER_BINDING_PROTOCOL  *This,
                                              IN EFI_HANDLE                          ControllerHandle,
                                              IN REFIND_EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath);
EFI_STATUS EFIAPI fsw_efi_DriverBinding_Stop(IN REFIND_EFI_DRIVER_BINDING_PROTOCOL  *This,
                                             IN EFI_HANDLE                          ControllerHandle,
                                             IN UINTN                               NumberOfChildren,
                                             IN EFI_HANDLE                          *ChildHandleBuffer);

EFI_STATUS EFIAPI fsw_efi_ComponentName_GetDriverName(IN  REFIND_EFI_COMPONENT_NAME_PROTOCOL  *This,
                                                      IN  CHAR8                               *Language,
                                                      OUT CHAR16                              **DriverName);
EFI_STATUS EFIAPI fsw_efi_ComponentName_GetControllerName(IN  REFIND_EFI_COMPONENT_NAME_PROTOCOL  *This,
                                                          IN  EFI_HANDLE                          ControllerHandle,
                                                          IN  EFI_HANDLE                          ChildHandle  OPTIONAL,
                                                          IN  CHAR8                               *Language,
                                                          OUT CHAR16                              **ControllerName);

void EFIAPI fsw_efi_change_blocksize(struct fsw_volume *vol,
                              fsw_u32 old_phys_blocksize, fsw_u32 old_log_blocksize,
                              fsw_u32 new_phys_blocksize, fsw_u32 new_log_blocksize);
fsw_status_t EFIAPI fsw_efi_read_block(struct fsw_volume *vol, fsw_u64 phys_bno, void *buffer);

EFI_STATUS fsw_efi_map_status(fsw_status_t fsw_status, FSW_VOLUME_DATA *Volume);

EFI_STATUS EFIAPI fsw_efi_FileSystem_OpenVolume(IN EFI_FILE_IO_INTERFACE *This,
                                                OUT EFI_FILE_PROTOCOL **Root);
EFI_STATUS fsw_efi_dnode_to_FileHandle(IN struct fsw_dnode *dno,
                                       OUT EFI_FILE_PROTOCOL **NewFileHandle);

EFI_STATUS fsw_efi_file_read(IN FSW_FILE_DATA *File,
                             IN OUT UINTN *BufferSize,
                             OUT VOID *Buffer);
EFI_STATUS fsw_efi_file_getpos(IN FSW_FILE_DATA *File,
                               OUT UINT64 *Position);
EFI_STATUS fsw_efi_file_setpos(IN FSW_FILE_DATA *File,
                               IN UINT64 Position);

EFI_STATUS fsw_efi_dir_open(IN FSW_FILE_DATA *File,
                            OUT EFI_FILE_PROTOCOL **NewHandle,
                            IN CHAR16 *FileName,
                            IN UINT64 OpenMode,
                            IN UINT64 Attributes);
EFI_STATUS fsw_efi_dir_read(IN FSW_FILE_DATA *File,
                            IN OUT UINTN *BufferSize,
                            OUT VOID *Buffer);
EFI_STATUS fsw_efi_dir_setpos(IN FSW_FILE_DATA *File,
                              IN UINT64 Position);

EFI_STATUS fsw_efi_dnode_getinfo(IN FSW_FILE_DATA *File,
                                 IN EFI_GUID *InformationType,
                                 IN OUT UINTN *BufferSize,
                                 OUT VOID *Buffer);
EFI_STATUS fsw_efi_dnode_fill_FileInfo(IN FSW_VOLUME_DATA *Volume,
                                       IN struct fsw_dnode *dno,
                                       IN OUT UINTN *BufferSize,
                                       OUT VOID *Buffer);

 



#define CACHE_SIZE 131072  
struct cache_data {
   fsw_u8            *Cache;
   fsw_u64           CacheStart;
   BOOLEAN           CacheValid;
   FSW_VOLUME_DATA   *Volume;  
};

#define NUM_CACHES 2  
static struct cache_data    Caches[NUM_CACHES];
static int LastRead = -1;

 



REFIND_EFI_DRIVER_BINDING_PROTOCOL fsw_efi_DriverBinding_table = {
    fsw_efi_DriverBinding_Supported,
    fsw_efi_DriverBinding_Start,
    fsw_efi_DriverBinding_Stop,
    0x10,
    NULL,
    NULL
};

 



REFIND_EFI_COMPONENT_NAME_PROTOCOL fsw_efi_ComponentName_table = {
    fsw_efi_ComponentName_GetDriverName,
    fsw_efi_ComponentName_GetControllerName,
    (CHAR8*) "eng"
};

 



struct fsw_host_table   fsw_efi_host_table = {
    FSW_STRING_TYPE_UTF16,

    fsw_efi_change_blocksize,
    fsw_efi_read_block
};

extern struct fsw_fstype_table   FSW_FSTYPE_TABLE_NAME(FSTYPE);


VOID EFIAPI fsw_efi_clear_cache(VOID) {
   int i;

    
   for (i = 0; i < NUM_CACHES; i++) {
      if (Caches[i].Cache != NULL) {
         FreePool(Caches[i].Cache);
         Caches[i].Cache = NULL;
      }  
            Caches[i].CacheStart = 0;
            Caches[i].CacheValid = FALSE;
            Caches[i].Volume = NULL;
   }
   LastRead = -1;
}  

 




EFI_STATUS EFIAPI fsw_efi_main(IN EFI_HANDLE         ImageHandle,
                               IN EFI_SYSTEM_TABLE   *SystemTable)
{
    EFI_STATUS  Status;

#ifndef __MAKEWITH_TIANO
     
    InitializeLib(ImageHandle, SystemTable);
#endif

     
    fsw_efi_DriverBinding_table.ImageHandle          = ImageHandle;
    fsw_efi_DriverBinding_table.DriverBindingHandle  = ImageHandle;
     
    Status = refit_call4_wrapper(BS->InstallProtocolInterface, &fsw_efi_DriverBinding_table.DriverBindingHandle,
                                          &gMyEfiDriverBindingProtocolGuid,
                                          EFI_NATIVE_INTERFACE,
                                          &fsw_efi_DriverBinding_table);
    if (EFI_ERROR (Status)) {
        return Status;
    }

     
    Status = refit_call4_wrapper(BS->InstallProtocolInterface, &fsw_efi_DriverBinding_table.DriverBindingHandle,
                                          &gMyEfiComponentNameProtocolGuid,
                                          EFI_NATIVE_INTERFACE,
                                          &fsw_efi_ComponentName_table);
    if (EFI_ERROR (Status)) {
        return Status;
    }

 
 
 
 
 
 
 
 

    return EFI_SUCCESS;
}

#ifdef __MAKEWITH_GNUEFI
EFI_DRIVER_ENTRY_POINT(fsw_efi_main)
#endif

 






EFI_STATUS EFIAPI fsw_efi_DriverBinding_Supported(IN REFIND_EFI_DRIVER_BINDING_PROTOCOL  *This,
                                                  IN EFI_HANDLE                   ControllerHandle,
                                                  IN REFIND_EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath)
{
    EFI_STATUS          Status;
    EFI_DISK_IO         *DiskIo;

     

     
    Status = refit_call6_wrapper(BS->OpenProtocol, ControllerHandle,
                              &gMyEfiDiskIoProtocolGuid,
                              (VOID **) &DiskIo,
                              This->DriverBindingHandle,
                              ControllerHandle,
                              EFI_OPEN_PROTOCOL_BY_DRIVER);
    if (EFI_ERROR(Status))
        return Status;

     
    refit_call4_wrapper(BS->CloseProtocol, ControllerHandle,
                      &gMyEfiDiskIoProtocolGuid,
                      This->DriverBindingHandle,
                      ControllerHandle);

     
    Status = refit_call6_wrapper(BS->OpenProtocol, ControllerHandle,
                              &gMyEfiBlockIoProtocolGuid,
                              NULL,
                              This->DriverBindingHandle,
                              ControllerHandle,
                              EFI_OPEN_PROTOCOL_TEST_PROTOCOL);
    return Status;
}

 












/*
 * Opens a controller's block/disk protocols and mounts a backend before
 * installing Simple File System. An error must unwind the opened protocols.
 * AUDIT: Volume is dereferenced immediately after AllocateZeroPool; review
 * allocation-failure handling before calling this path resource-safe.
 */
EFI_STATUS EFIAPI fsw_efi_DriverBinding_Start(IN REFIND_EFI_DRIVER_BINDING_PROTOCOL  *This,
                                              IN EFI_HANDLE                   ControllerHandle,
                                              IN REFIND_EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath)
{
    EFI_STATUS          Status;
    EFI_BLOCK_IO        *BlockIo;
    EFI_DISK_IO         *DiskIo;
    FSW_VOLUME_DATA     *Volume;

#if DEBUG_LEVEL
    Print(L"fsw_efi_DriverBinding_Start\n");
#endif

     
    Status = refit_call6_wrapper(BS->OpenProtocol, ControllerHandle,
                              &gMyEfiBlockIoProtocolGuid,
                              (VOID **) &BlockIo,
                              This->DriverBindingHandle,
                              ControllerHandle,
                              EFI_OPEN_PROTOCOL_GET_PROTOCOL);    
    if (EFI_ERROR(Status)) {
 
        return Status;
    }

    Status = refit_call6_wrapper(BS->OpenProtocol, ControllerHandle,
                              &gMyEfiDiskIoProtocolGuid,
                              (VOID **) &DiskIo,
                              This->DriverBindingHandle,
                              ControllerHandle,
                              EFI_OPEN_PROTOCOL_BY_DRIVER);
    if (EFI_ERROR(Status)) {
        Print(L"Fsw ERROR: OpenProtocol(DiskIo) returned %x\n", Status);
        return Status;
    }

     
    Volume = AllocateZeroPool(sizeof(FSW_VOLUME_DATA));
    Volume->Signature       = FSW_VOLUME_DATA_SIGNATURE;
    Volume->Handle          = ControllerHandle;
    Volume->DiskIo          = DiskIo;
    Volume->MediaId         = BlockIo->Media->MediaId;
    Volume->LastIOStatus    = EFI_SUCCESS;

     
    Status = fsw_efi_map_status(fsw_mount(Volume, &fsw_efi_host_table,
                                          &FSW_FSTYPE_TABLE_NAME(FSTYPE), &Volume->vol),
                                Volume);
    if (!EFI_ERROR(Status)) {
         
        Volume->FileSystem.Revision     = EFI_FILE_IO_INTERFACE_REVISION;
        Volume->FileSystem.OpenVolume   = fsw_efi_FileSystem_OpenVolume;
        Status = refit_call4_wrapper(BS->InstallMultipleProtocolInterfaces, &ControllerHandle,
                                                       &gMyEfiSimpleFileSystemProtocolGuid,
                                                       &Volume->FileSystem,
                                                       NULL);
        if (EFI_ERROR(Status)) {
 
        }
    }

     
    if (EFI_ERROR(Status)) {
        if (Volume->vol != NULL)
            fsw_unmount(Volume->vol);
        FreePool(Volume);

        refit_call4_wrapper(BS->CloseProtocol, ControllerHandle,
                          &gMyEfiDiskIoProtocolGuid,
                          This->DriverBindingHandle,
                          ControllerHandle);
    }
    return Status;
}

 









/*
 * Unpublish Simple File System before freeing the embedded protocol's
 * containing volume. If firmware refuses the uninstall, return immediately
 * while that volume is still live. After detachment, unmount FSW, close Disk
 * I/O and clear the read cache.
 */
EFI_STATUS EFIAPI fsw_efi_DriverBinding_Stop(IN  REFIND_EFI_DRIVER_BINDING_PROTOCOL  *This,
                                             IN  EFI_HANDLE                   ControllerHandle,
                                             IN  UINTN                        NumberOfChildren,
                                             IN  EFI_HANDLE                   *ChildHandleBuffer)
{
    EFI_STATUS          Status;
    EFI_FILE_IO_INTERFACE *FileSystem;
    FSW_VOLUME_DATA     *Volume;

#if DEBUG_LEVEL
    Print(L"fsw_efi_DriverBinding_Stop\n");
#endif

     
    Status = refit_call6_wrapper(BS->OpenProtocol, ControllerHandle,
                              &gMyEfiSimpleFileSystemProtocolGuid,
                              (VOID **) &FileSystem,
                              This->DriverBindingHandle,
                              ControllerHandle,
                              EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(Status))
        return EFI_UNSUPPORTED;

     
    Volume = FSW_VOLUME_FROM_FILE_SYSTEM(FileSystem);

     
    Status = refit_call4_wrapper(BS->UninstallMultipleProtocolInterfaces, ControllerHandle,
                                                     &gMyEfiSimpleFileSystemProtocolGuid, &Volume->FileSystem,
                                                     NULL);
    if (EFI_ERROR(Status)) {
  
        return Status;
    }
#if DEBUG_LEVEL
    Print(L"fsw_efi_DriverBinding_Stop: protocol uninstalled successfully\n");
#endif

     
    if (Volume->vol != NULL)
        fsw_unmount(Volume->vol);
    FreePool(Volume);

     
    Status = refit_call4_wrapper(BS->CloseProtocol, ControllerHandle,
                               &gMyEfiDiskIoProtocolGuid,
                               This->DriverBindingHandle,
                               ControllerHandle);

     
    fsw_efi_clear_cache();

    return Status;
}

 





EFI_STATUS EFIAPI fsw_efi_ComponentName_GetDriverName(IN  REFIND_EFI_COMPONENT_NAME_PROTOCOL  *This,
                                                      IN  CHAR8                        *Language,
                                                      OUT CHAR16                       **DriverName)
{
    if (Language == NULL || DriverName == NULL)
        return EFI_INVALID_PARAMETER;

    if (Language[0] == 'e' && Language[1] == 'n' && Language[2] == 'g' && Language[3] == 0) {
        *DriverName = FSW_EFI_DRIVER_NAME(FSTYPE);
        return EFI_SUCCESS;
    }
    return EFI_UNSUPPORTED;
}

 




EFI_STATUS EFIAPI fsw_efi_ComponentName_GetControllerName(IN  REFIND_EFI_COMPONENT_NAME_PROTOCOL    *This,
                                                          IN  EFI_HANDLE                     ControllerHandle,
                                                          IN  EFI_HANDLE                     ChildHandle  OPTIONAL,
                                                          IN  CHAR8                          *Language,
                                                          OUT CHAR16                         **ControllerName)
{
    return EFI_UNSUPPORTED;
}

 




void EFIAPI fsw_efi_change_blocksize(struct fsw_volume *vol,
                              fsw_u32 old_phys_blocksize, fsw_u32 old_log_blocksize,
                              fsw_u32 new_phys_blocksize, fsw_u32 new_log_blocksize)
{
     
}

 










/*
 * Translate the FSW physical block number into a byte offset for EFI Disk
 * I/O. A cached window must match the volume and cover the requested
 * interval. When read-ahead cannot serve this request, the fallback asks
 * firmware for just the original block.
 */
fsw_status_t EFIAPI fsw_efi_read_block(struct fsw_volume *vol, fsw_u64 phys_bno, void *buffer) {
   int              i, ReadCache = -1;
   FSW_VOLUME_DATA  *Volume = (FSW_VOLUME_DATA *)vol->host_data;
   EFI_STATUS       Status = EFI_SUCCESS;
   BOOLEAN          ReadOneBlock = FALSE;
   UINT64           StartRead = (UINT64) phys_bno * (UINT64) vol->phys_blocksize;

   if (buffer == NULL)
      return (fsw_status_t) EFI_BAD_BUFFER_SIZE;

    
   if (LastRead < 0) {
      fsw_efi_clear_cache();
   }  

    
   i = 0;
   do {
      if ((Caches[i].Volume == Volume) &&
          (Caches[i].CacheValid == TRUE) &&
          (StartRead >= Caches[i].CacheStart) &&
          ((StartRead + vol->phys_blocksize) <= (Caches[i].CacheStart + CACHE_SIZE))) {
         ReadCache = i;
      }
      i++;
   } while ((i < NUM_CACHES) && (ReadCache < 0));

    
   if (ReadCache < 0) {
      if (LastRead == -1)
         LastRead = 1;
      ReadCache = 1 - LastRead;  
      Caches[ReadCache].CacheValid = FALSE;
      if (Caches[ReadCache].Cache == NULL)
         Caches[ReadCache].Cache = AllocatePool(CACHE_SIZE);
      if (Caches[ReadCache].Cache != NULL) {
          
          
          
          
          
          
          
          
          
          
         Status = refit_call5_wrapper(Volume->DiskIo->ReadDisk, Volume->DiskIo, Volume->MediaId,
                                      StartRead, (UINTN) CACHE_SIZE, (VOID*) Caches[ReadCache].Cache);
         if (!EFI_ERROR(Status)) {
            Caches[ReadCache].CacheStart = StartRead;
            Caches[ReadCache].CacheValid = TRUE;
            Caches[ReadCache].Volume = Volume;
            LastRead = ReadCache;
         } else {
            ReadOneBlock = TRUE;
         }
      } else {
         ReadOneBlock = TRUE;
      }  
   }  

   if (Caches[ReadCache].Cache != NULL && Caches[ReadCache].CacheValid == TRUE && vol->phys_blocksize > 0) {
      refit_call3_wrapper(gBS->CopyMem, buffer,
                          &Caches[ReadCache].Cache[StartRead - Caches[ReadCache].CacheStart],
                          vol->phys_blocksize);
   } else {
      ReadOneBlock = TRUE;
   }

   if (ReadOneBlock) {  
      Status = refit_call5_wrapper(Volume->DiskIo->ReadDisk, Volume->DiskIo, Volume->MediaId,
                                   phys_bno * vol->phys_blocksize,
                                   (UINTN) vol->phys_blocksize,
                                   (VOID*) buffer);
   }
   Volume->LastIOStatus = Status;

   return Status;
}  

 





/*
 * Maps filesystem-neutral errors into EFI_STATUS; translating unsupported
 * or corrupt media into success would expose invalid root handles.
 */
EFI_STATUS fsw_efi_map_status(fsw_status_t fsw_status, FSW_VOLUME_DATA *Volume)
{
    switch (fsw_status) {
        case FSW_SUCCESS:
            return EFI_SUCCESS;
        case FSW_OUT_OF_MEMORY:
            return EFI_VOLUME_CORRUPTED;
        case FSW_IO_ERROR:
            return Volume->LastIOStatus;
        case FSW_UNSUPPORTED:
            return EFI_UNSUPPORTED;
        case FSW_NOT_FOUND:
            return EFI_NOT_FOUND;
        case FSW_VOLUME_CORRUPTED:
            return EFI_VOLUME_CORRUPTED;
        default:
            return EFI_DEVICE_ERROR;
    }
}

 






EFI_STATUS EFIAPI fsw_efi_FileSystem_OpenVolume(IN EFI_FILE_IO_INTERFACE *This,
                                                OUT EFI_FILE_PROTOCOL **Root)
{
    EFI_STATUS          Status;
    FSW_VOLUME_DATA     *Volume = FSW_VOLUME_FROM_FILE_SYSTEM(This);

#if DEBUG_LEVEL
    Print(L"fsw_efi_FileSystem_OpenVolume\n");
#endif

    fsw_efi_clear_cache();
    Status = fsw_efi_dnode_to_FileHandle(Volume->vol->root, Root);

    return Status;
}

 




EFI_STATUS EFIAPI fsw_efi_FileHandle_Open(IN EFI_FILE_PROTOCOL *This,
                                          OUT EFI_FILE_PROTOCOL **NewHandle,
                                          IN CHAR16 *FileName,
                                          IN UINT64 OpenMode,
                                          IN UINT64 Attributes)
{
    FSW_FILE_DATA      *File = FSW_FILE_FROM_FILE_HANDLE(This);

    if (File->Type == FSW_EFI_FILE_TYPE_DIR)
        return fsw_efi_dir_open(File, NewHandle, FileName, OpenMode, Attributes);
     
    return EFI_UNSUPPORTED;
}

 




EFI_STATUS EFIAPI fsw_efi_FileHandle_Close(IN EFI_FILE_PROTOCOL *This)
{
    FSW_FILE_DATA      *File = FSW_FILE_FROM_FILE_HANDLE(This);

#if DEBUG_LEVEL
    Print(L"fsw_efi_FileHandle_Close\n");
#endif

    fsw_shandle_close(&File->shand);
    FreePool(File);

    return EFI_SUCCESS;
}

 




EFI_STATUS EFIAPI fsw_efi_FileHandle_Delete(IN EFI_FILE_PROTOCOL *This)
{
    EFI_STATUS          Status;

    Status = refit_call1_wrapper(This->Close, This);
    if (Status == EFI_SUCCESS) {
         
        Status = EFI_WARN_DELETE_FAILURE;
    }

    return Status;
}

 




/*
 * Translates EFI file-protocol Read semantics to filesystem-neutral handles;
 * returned buffer sizes and end-of-file state must follow EFI conventions.
 */
EFI_STATUS EFIAPI fsw_efi_FileHandle_Read(IN EFI_FILE_PROTOCOL *This,
                                          IN OUT UINTN *BufferSize,
                                          OUT VOID *Buffer)
{
    FSW_FILE_DATA      *File = FSW_FILE_FROM_FILE_HANDLE(This);

    if (File->Type == FSW_EFI_FILE_TYPE_FILE)
        return fsw_efi_file_read(File, BufferSize, Buffer);
    else if (File->Type == FSW_EFI_FILE_TYPE_DIR)
        return fsw_efi_dir_read(File, BufferSize, Buffer);
    return EFI_UNSUPPORTED;
}

 




EFI_STATUS EFIAPI fsw_efi_FileHandle_Write(IN EFI_FILE_PROTOCOL *This,
                                           IN OUT UINTN *BufferSize,
                                           IN VOID *Buffer)
{
     
    return EFI_WRITE_PROTECTED;
}

 




EFI_STATUS EFIAPI fsw_efi_FileHandle_GetPosition(IN EFI_FILE_PROTOCOL *This,
                                                 OUT UINT64 *Position)
{
    FSW_FILE_DATA      *File = FSW_FILE_FROM_FILE_HANDLE(This);

    if (File->Type == FSW_EFI_FILE_TYPE_FILE)
        return fsw_efi_file_getpos(File, Position);
     
    return EFI_UNSUPPORTED;
}

 




EFI_STATUS EFIAPI fsw_efi_FileHandle_SetPosition(IN EFI_FILE_PROTOCOL *This,
                                                 IN UINT64 Position)
{
    FSW_FILE_DATA      *File = FSW_FILE_FROM_FILE_HANDLE(This);

    if (File->Type == FSW_EFI_FILE_TYPE_FILE)
        return fsw_efi_file_setpos(File, Position);
    else if (File->Type == FSW_EFI_FILE_TYPE_DIR)
        return fsw_efi_dir_setpos(File, Position);
    return EFI_UNSUPPORTED;
}

 




EFI_STATUS EFIAPI fsw_efi_FileHandle_GetInfo(IN EFI_FILE_PROTOCOL *This,
                                             IN EFI_GUID *InformationType,
                                             IN OUT UINTN *BufferSize,
                                             OUT VOID *Buffer)
{
    FSW_FILE_DATA      *File = FSW_FILE_FROM_FILE_HANDLE(This);

    return fsw_efi_dnode_getinfo(File, InformationType, BufferSize, Buffer);
}

 




EFI_STATUS EFIAPI fsw_efi_FileHandle_SetInfo(IN EFI_FILE_PROTOCOL *This,
                                             IN EFI_GUID *InformationType,
                                             IN UINTN BufferSize,
                                             IN VOID *Buffer)
{
     
    return EFI_WRITE_PROTECTED;
}

 




EFI_STATUS EFIAPI fsw_efi_FileHandle_Flush(IN EFI_FILE_PROTOCOL *This)
{
     
    return EFI_WRITE_PROTECTED;
}

 





EFI_STATUS fsw_efi_dnode_to_FileHandle(IN struct fsw_dnode *dno,
                                       OUT EFI_FILE_PROTOCOL **NewFileHandle)
{
    EFI_STATUS          Status;
    FSW_FILE_DATA       *File;

     
    Status = fsw_efi_map_status(fsw_dnode_fill(dno), (FSW_VOLUME_DATA *)dno->vol->host_data);
    if (EFI_ERROR(Status))
        return Status;

     
    if (dno->type != FSW_DNODE_TYPE_FILE && dno->type != FSW_DNODE_TYPE_DIR)
        return EFI_UNSUPPORTED;

     
    File = AllocateZeroPool(sizeof(FSW_FILE_DATA));
    File->Signature = FSW_FILE_DATA_SIGNATURE;
    if (dno->type == FSW_DNODE_TYPE_FILE)
        File->Type = FSW_EFI_FILE_TYPE_FILE;
    else if (dno->type == FSW_DNODE_TYPE_DIR)
        File->Type = FSW_EFI_FILE_TYPE_DIR;

     
    Status = fsw_efi_map_status(fsw_shandle_open(dno, &File->shand),
                                (FSW_VOLUME_DATA *)dno->vol->host_data);
    if (EFI_ERROR(Status)) {
        FreePool(File);
        return Status;
    }

     
    File->FileHandle.Revision    = EFI_FILE_HANDLE_REVISION;
    File->FileHandle.Open        = fsw_efi_FileHandle_Open;
    File->FileHandle.Close       = fsw_efi_FileHandle_Close;
    File->FileHandle.Delete      = fsw_efi_FileHandle_Delete;
    File->FileHandle.Read        = fsw_efi_FileHandle_Read;
    File->FileHandle.Write       = fsw_efi_FileHandle_Write;
    File->FileHandle.GetPosition = fsw_efi_FileHandle_GetPosition;
    File->FileHandle.SetPosition = fsw_efi_FileHandle_SetPosition;
    File->FileHandle.GetInfo     = fsw_efi_FileHandle_GetInfo;
    File->FileHandle.SetInfo     = fsw_efi_FileHandle_SetInfo;
    File->FileHandle.Flush       = fsw_efi_FileHandle_Flush;

    *NewFileHandle = &File->FileHandle;
    return EFI_SUCCESS;
}

 



EFI_STATUS fsw_efi_file_read(IN FSW_FILE_DATA *File,
                             IN OUT UINTN *BufferSize,
                             OUT VOID *Buffer)
{
    EFI_STATUS          Status;
    fsw_u32             buffer_size;

#if DEBUG_LEVEL
    Print(L"fsw_efi_file_read %d bytes\n", *BufferSize);
#endif

    buffer_size = (fsw_u32)*BufferSize;
    Status = fsw_efi_map_status(fsw_shandle_read(&File->shand, &buffer_size, Buffer),
                                (FSW_VOLUME_DATA *)File->shand.dnode->vol->host_data);
    *BufferSize = buffer_size;

    return Status;
}

 



EFI_STATUS fsw_efi_file_getpos(IN FSW_FILE_DATA *File,
                               OUT UINT64 *Position)
{
    *Position = File->shand.pos;
    return EFI_SUCCESS;
}

 




EFI_STATUS fsw_efi_file_setpos(IN FSW_FILE_DATA *File, IN UINT64 Position)
{
    if (Position == 0xFFFFFFFFFFFFFFFFULL)
        File->shand.pos = File->shand.dnode->size;
    else
        File->shand.pos = Position;
    return EFI_SUCCESS;
}

 







EFI_STATUS fsw_efi_dir_open(IN FSW_FILE_DATA *File,
                            OUT EFI_FILE_PROTOCOL **NewHandle,
                            IN CHAR16 *FileName,
                            IN UINT64 OpenMode,
                            IN UINT64 Attributes)
{
    EFI_STATUS          Status;
    FSW_VOLUME_DATA     *Volume = (FSW_VOLUME_DATA *)File->shand.dnode->vol->host_data;
    struct fsw_dnode    *dno;
    struct fsw_dnode    *target_dno;
    struct fsw_string   lookup_path;

#if DEBUG_LEVEL
    Print(L"fsw_efi_dir_open: '%s'\n", FileName);
#endif

    if (OpenMode != EFI_FILE_MODE_READ)
        return EFI_WRITE_PROTECTED;

    lookup_path.type = FSW_STRING_TYPE_UTF16;
    lookup_path.len  = (int)StrLen(FileName);
    lookup_path.size = lookup_path.len * sizeof(fsw_u16);
    lookup_path.data = FileName;

     
    Status = fsw_efi_map_status(fsw_dnode_lookup_path(File->shand.dnode, &lookup_path, '\\', &dno), Volume);
    if (EFI_ERROR(Status))
        return Status;

     
    Status = fsw_efi_map_status(fsw_dnode_resolve(dno, &target_dno), Volume);
    fsw_dnode_release(dno);
    if (EFI_ERROR(Status))
        return Status;
    dno = target_dno;

     
    Status = fsw_efi_dnode_to_FileHandle(dno, NewHandle);
    fsw_dnode_release(dno);
    return Status;
}

 




EFI_STATUS fsw_efi_dir_read(IN FSW_FILE_DATA *File,
                            IN OUT UINTN *BufferSize,
                            OUT VOID *Buffer)
{
    EFI_STATUS          Status;
    FSW_VOLUME_DATA     *Volume = (FSW_VOLUME_DATA *)File->shand.dnode->vol->host_data;
    struct fsw_dnode    *dno;

#if DEBUG_LEVEL
    Print(L"fsw_efi_dir_read...\n");
#endif

     
    Status = fsw_efi_map_status(fsw_dnode_dir_read(&File->shand, &dno), Volume);
    if (Status == EFI_NOT_FOUND) {
         
        *BufferSize = 0;
#if DEBUG_LEVEL
        Print(L"...no more entries\n");
#endif
        return EFI_SUCCESS;
    }
    if (EFI_ERROR(Status))
        return Status;
    /*
     * The backend has already advanced the directory cursor. A size
     * negotiation failure from FileInfo therefore leaves it past this entry.
     * The cursor rollback in fsw_dnode_dir_read covers backend errors only.
     */
    Status = fsw_efi_dnode_fill_FileInfo(Volume, dno, BufferSize, Buffer);
    fsw_dnode_release(dno);
    return Status;
}

 
/*
 * UEFI directories support a rewind here. Other numeric positions would
 * expose an internal directory cookie whose interpretation depends on the
 * filesystem.
 */
EFI_STATUS fsw_efi_dir_setpos(IN FSW_FILE_DATA *File, IN UINT64 Position)
{
    if (Position == 0) {
        File->shand.pos = 0;
        return EFI_SUCCESS;
    } else {
         
        return EFI_UNSUPPORTED;
    }
}

 





EFI_STATUS fsw_efi_dnode_getinfo(IN FSW_FILE_DATA *File,
                                 IN EFI_GUID *InformationType,
                                 IN OUT UINTN *BufferSize,
                                 OUT VOID *Buffer)
{
    EFI_STATUS            Status;
    FSW_VOLUME_DATA       *Volume = (FSW_VOLUME_DATA *)File->shand.dnode->vol->host_data;
    EFI_FILE_SYSTEM_INFO  *FSInfo;
    UINTN                 RequiredSize;
    struct fsw_volume_stat vsb;


    if (CompareGuid(InformationType, &gMyEfiFileInfoGuid)) {
#if DEBUG_LEVEL
        Print(L"fsw_efi_dnode_getinfo: FILE_INFO\n");
#endif

        Status = fsw_efi_dnode_fill_FileInfo(Volume, File->shand.dnode, BufferSize, Buffer);

    } else if (CompareGuid(InformationType, &gMyEfiFileSystemInfoGuid)) {
#if DEBUG_LEVEL
        Print(L"fsw_efi_dnode_getinfo: FILE_SYSTEM_INFO\n");
#endif

         
        RequiredSize = SIZE_OF_EFI_FILE_SYSTEM_INFO + fsw_efi_strsize(&Volume->vol->label);
        if (*BufferSize < RequiredSize) {
            *BufferSize = RequiredSize;
            return EFI_BUFFER_TOO_SMALL;
        }

         
        FSInfo = (EFI_FILE_SYSTEM_INFO *)Buffer;
        FSInfo->Size        = RequiredSize;
        FSInfo->ReadOnly    = TRUE;
        FSInfo->BlockSize   = Volume->vol->log_blocksize;
        fsw_efi_strcpy(FSInfo->VolumeLabel, &Volume->vol->label);

         
        ZeroMem(&vsb, sizeof(struct fsw_volume_stat));
        Status = fsw_efi_map_status(fsw_volume_stat(Volume->vol, &vsb), Volume);
        if (EFI_ERROR(Status))
            return Status;
        FSInfo->VolumeSize  = vsb.total_bytes;
        FSInfo->FreeSpace   = vsb.free_bytes;

         
        *BufferSize = RequiredSize;
        Status = EFI_SUCCESS;

    } else if (CompareGuid(InformationType, &gMyEfiFileSystemVolumeLabelInfoIdGuid)) {
#if DEBUG_LEVEL
        Print(L"fsw_efi_dnode_getinfo: FILE_SYSTEM_VOLUME_LABEL\n");
#endif

         
        RequiredSize = SIZE_OF_EFI_FILE_SYSTEM_VOLUME_LABEL_INFO + fsw_efi_strsize(&Volume->vol->label);
        if (*BufferSize < RequiredSize) {
            *BufferSize = RequiredSize;
            return EFI_BUFFER_TOO_SMALL;
        }

         
        fsw_efi_strcpy(((EFI_FILE_SYSTEM_VOLUME_LABEL_INFO *)Buffer)->VolumeLabel, &Volume->vol->label);

         
        *BufferSize = RequiredSize;
        Status = EFI_SUCCESS;

    } else {
        Status = EFI_UNSUPPORTED;
    }

    return Status;
}

 





void fsw_store_time_posix(struct fsw_dnode_stat *sb, int which, fsw_u32 posix_time)
{
    EFI_FILE_INFO       *FileInfo = (EFI_FILE_INFO *)sb->host_data;

    if (which == FSW_DNODE_STAT_CTIME)
        fsw_efi_decode_time(&FileInfo->CreateTime,       posix_time);
    else if (which == FSW_DNODE_STAT_MTIME)
        fsw_efi_decode_time(&FileInfo->ModificationTime, posix_time);
    else if (which == FSW_DNODE_STAT_ATIME)
        fsw_efi_decode_time(&FileInfo->LastAccessTime,   posix_time);
}

 





void fsw_store_attr_posix(struct fsw_dnode_stat *sb, fsw_u16 posix_mode)
{
    EFI_FILE_INFO       *FileInfo = (EFI_FILE_INFO *)sb->host_data;

    if ((posix_mode & S_IWUSR) == 0)
        FileInfo->Attribute |= EFI_FILE_READ_ONLY;
}

void fsw_store_attr_efi(struct fsw_dnode_stat *sb, fsw_u16 attr)
{
    EFI_FILE_INFO       *FileInfo = (EFI_FILE_INFO *)sb->host_data;

    FileInfo->Attribute |= attr;
}

 



EFI_STATUS fsw_efi_dnode_fill_FileInfo(IN FSW_VOLUME_DATA *Volume,
                                       IN struct fsw_dnode *dno,
                                       IN OUT UINTN *BufferSize,
                                       OUT VOID *Buffer)
{
    EFI_STATUS          Status;
    EFI_FILE_INFO       *FileInfo;
    UINTN               RequiredSize;
    struct fsw_dnode_stat sb;

     
    Status = fsw_efi_map_status(fsw_dnode_fill(dno), Volume);
    if (EFI_ERROR(Status))
        return Status;

     

     
    RequiredSize = SIZE_OF_EFI_FILE_INFO + fsw_efi_strsize(&dno->name);
    if (*BufferSize < RequiredSize) {
         

#if DEBUG_LEVEL
        Print(L"...BUFFER TOO SMALL\n");
#endif
        *BufferSize = RequiredSize;
        return EFI_BUFFER_TOO_SMALL;
    }

     
    ZeroMem(Buffer, RequiredSize);
    FileInfo = (EFI_FILE_INFO *)Buffer;
    FileInfo->Size = RequiredSize;
    FileInfo->FileSize          = dno->size;
    FileInfo->Attribute         = 0;
    if (dno->type == FSW_DNODE_TYPE_DIR)
        FileInfo->Attribute    |= EFI_FILE_DIRECTORY;
    fsw_efi_strcpy(FileInfo->FileName, &dno->name);

     
    ZeroMem(&sb, sizeof(struct fsw_dnode_stat));
    sb.host_data = FileInfo;
    Status = fsw_efi_map_status(fsw_dnode_stat(dno, &sb), Volume);
    if (EFI_ERROR(Status))
        return Status;
    FileInfo->PhysicalSize      = sb.used_bytes;

     
    *BufferSize = RequiredSize;
#if DEBUG_LEVEL
    Print(L"...returning '%s'\n", FileInfo->FileName);
#endif
    return EFI_SUCCESS;
}

 
