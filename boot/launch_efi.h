 


































 






 














#ifndef __BOOT_LAUNCH_EFI_H_
#define __BOOT_LAUNCH_EFI_H_

#ifdef __MAKEWITH_GNUEFI
#include "efi.h"
#include "efilib.h"
#else
#include "../include/tiano_includes.h"
#endif
#include "global.h"

#ifndef EFI_OS_INDICATIONS_BOOT_TO_FW_UI
#define EFI_OS_INDICATIONS_BOOT_TO_FW_UI 0x0000000000000001ULL
#endif

 
#define LOADER_TYPE_INVALID 0
#define LOADER_TYPE_EFI     1
#define LOADER_TYPE_GZIP    2

EFI_STATUS StartEFIImage(IN REFIT_VOLUME *Volume,
                         IN CHAR16 *Filename,
                         IN CHAR16 *LoadOptions,
                         IN CHAR16 *ImageTitle,
                         IN CHAR8 OSType,
                         IN BOOLEAN Verbose,
                         IN BOOLEAN IsDriver);
UINTN IsValidLoader(EFI_FILE_PROTOCOL *RootDir, CHAR16 *FileName);
EFI_STATUS RebootIntoFirmware(VOID);
VOID StartLoader(LOADER_ENTRY *Entry, CHAR16 *SelectionName);
VOID StartTool(IN LOADER_ENTRY *Entry);
VOID RebootIntoLoader(LOADER_ENTRY *Entry);

#endif

 
