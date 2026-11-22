/* EFI file helpers for security inspection.
 * Opened files require Close; device and filesystem protocols remain
 * firmware-owned. Read buffers have independent allocation lifetimes.
 */

#include <global.h>
#include "../include/refit_call_wrapper.h"

#include "simple_file.h"
#include "../boot/lib.h"

static EFI_GUID IMAGE_PROTOCOL = LOADED_IMAGE_PROTOCOL;
static EFI_GUID SIMPLE_FS_PROTOCOL = SIMPLE_FILE_SYSTEM_PROTOCOL;
static EFI_GUID FILE_INFO = EFI_FILE_INFO_ID;

/*
 * Opens a named EFI file on a supplied device handle. Returning success
 * requires a usable file protocol, not merely a syntactically valid path.
 */
EFI_STATUS
simple_file_open_by_handle(EFI_HANDLE device, CHAR16 *name, EFI_FILE_PROTOCOL **file, UINT64 mode)
{
   EFI_STATUS efi_status;
   EFI_FILE_IO_INTERFACE *drive;
   EFI_FILE_PROTOCOL *root;

   efi_status = uefi_call_wrapper(BS->HandleProtocol, 3, device,
                   &SIMPLE_FS_PROTOCOL, (VOID**) &drive);

   if (efi_status != EFI_SUCCESS) {
      Print(L"Unable to find simple file protocol\n");
      goto error;
   }

   efi_status = uefi_call_wrapper(drive->OpenVolume, 2, drive, &root);

   if (efi_status != EFI_SUCCESS) {
      Print(L"Failed to open drive volume\n");
      goto error;
   }

   efi_status = uefi_call_wrapper(root->Open, 5, root, file, name,
                   mode, 0);

 error:
   return efi_status;
}

static
EFI_STATUS
generate_path(CHAR16* name, EFI_LOADED_IMAGE *li, EFI_DEVICE_PATH **path, CHAR16 **PathName)
{
        unsigned int pathlen;
        EFI_STATUS efi_status = EFI_SUCCESS;
        CHAR16 *devpathstr = DevicePathToStr(li->FilePath),
                *found = NULL;
        int i;

        for (i = 0; i < StrLen(devpathstr); i++) {
                if (devpathstr[i] == '/')
                        devpathstr[i] = '\\';
                if (devpathstr[i] == '\\')
                        found = &devpathstr[i];
        }
        if (!found) {
                pathlen = 0;
        } else {
                while (*(found - 1) == '\\')
                        --found;
                *found = '\0';
                pathlen = StrLen(devpathstr);
        }

        if (name[0] != '\\')
                pathlen++;

        *PathName = AllocatePool((pathlen + 1 + StrLen(name))*sizeof(CHAR16));

        if (!*PathName) {
                Print(L"Failed to allocate path buffer\n");
                efi_status = EFI_OUT_OF_RESOURCES;
                goto error;
        }

        StrCpy(*PathName, devpathstr);

        if (name[0] != '\\')
                StrCat(*PathName, L"\\");
        StrCat(*PathName, name);

        *path = FileDevicePath(li->DeviceHandle, *PathName);

error:
        MyFreePool(devpathstr);

        return efi_status;
}  

EFI_STATUS
/*
 * Resolves an image-relative EFI pathname to a file handle using device
 * protocols. The resolved firmware path and open result are separate from
 * the identity and signature of the file's contents.
 */
simple_file_open(EFI_HANDLE image, CHAR16 *name, EFI_FILE_PROTOCOL **file, UINT64 mode)
{
   EFI_STATUS efi_status;
   EFI_HANDLE device;
   EFI_LOADED_IMAGE *li;
   EFI_DEVICE_PATH *loadpath = NULL;
   CHAR16 *PathName = NULL;

   efi_status = uefi_call_wrapper(BS->HandleProtocol, 3, image,
                   &IMAGE_PROTOCOL, (VOID**) &li);

   if (efi_status != EFI_SUCCESS)
      return simple_file_open_by_handle(image, name, file, mode);

   efi_status = generate_path(name, li, &loadpath, &PathName);

   if (efi_status != EFI_SUCCESS) {
      Print(L"Unable to generate load path for %s\n", name);
      return efi_status;
   }

   device = li->DeviceHandle;

   efi_status = simple_file_open_by_handle(device, PathName, file, mode);

   MyFreePool(PathName);
   MyFreePool(loadpath);

   return efi_status;
}

/*
 * Use GetInfo to obtain the advertised size, allocate that many bytes, and
 * let Read report the actual count through size. Once allocation succeeds the
 * buffer is exposed through the out-parameter, including if the subsequent
 * read returns an error.
 */
EFI_STATUS
simple_file_read_all(EFI_FILE_PROTOCOL *file, UINTN *size, void **buffer)
{
   EFI_STATUS efi_status;
   EFI_FILE_INFO *fi;
   char buf[1024];

   *size = sizeof(buf);
   fi = (void *)buf;

   efi_status = uefi_call_wrapper(file->GetInfo, 4, file, &FILE_INFO,
                   size, fi);
   if (efi_status != EFI_SUCCESS) {
      Print(L"Failed to get file info\n");
      return efi_status;
   }

   *size = fi->FileSize;

   *buffer = AllocatePool(*size);
   if (!*buffer) {
      Print(L"Failed to allocate buffer of size %d\n", *size);
      return EFI_OUT_OF_RESOURCES;
   }
   efi_status = uefi_call_wrapper(file->Read, 3, file, size, *buffer);

   return efi_status;
}

void
/*
 * Releases the EFI file protocol handle; callers may not reuse it afterwards.
 */
simple_file_close(EFI_FILE_PROTOCOL *file)
{
   uefi_call_wrapper(file->Close, 1, file);
}
