/*
 * Configuration-file structures and defaults consumed by ReadConfig.
 * Default exclusion patterns and scan directories determine menu visibility,
 * not permission to delete EFI executables. Values are compiled for specific
 * EFI target architectures; the sample file can override runtime defaults.
 */
 



































 








#ifndef __CONFIG_H_
#define __CONFIG_H_

#ifdef __MAKEWITH_GNUEFI
#include "efi.h"
#else
#include "../include/tiano_includes.h"
#endif
#include "global.h"


 
 
 

typedef struct {
    UINT8   *Buffer;
    UINTN   BufferSize;
    UINTN   Encoding;
    CHAR8   *Current8Ptr;
    CHAR8   *End8Ptr;
    CHAR16  *Current16Ptr;
    CHAR16  *End16Ptr;
} REFIT_FILE;

#define CONFIG_FILE_NAME         L"boot.conf"
 
#if defined (EFIX64)
#define DONT_SCAN_FILES L"shim.efi,shim-fedora.efi,shim-centos.efi,shimx64.efi,PreLoader.efi,fbx64.efi"
#elif defined(EFI32)
#define DONT_SCAN_FILES L"shim.efi,shim-fedora.efi,shim-centos.efi,shimx64.efi,PreLoader.efi,fbia32.efi"
#elif defined(EFIAARCH64)
#define DONT_SCAN_FILES L"shim.efi,shim-fedora.efi,shim-centos.efi,shimx64.efi,PreLoader.efi,fbaa64.efi"
#else
#define DONT_SCAN_FILES L"shim.efi,shim-fedora.efi,shim-centos.efi,shimx64.efi,PreLoader.efi,fb.efi"
#endif
#define DONT_SCAN_VOLUMES L"LRS_ESP"
#define ALSO_SCAN_DIRS L"boot,@/boot"

EFI_STATUS ReadFile(IN EFI_FILE_HANDLE BaseDir, CHAR16 *FileName, REFIT_FILE *File, UINTN *size);
VOID ReadConfig(CHAR16 *FileName);
VOID ScanUserConfigured(CHAR16 *FileName);
UINTN ReadTokenLine(IN REFIT_FILE *File, OUT CHAR16 ***TokenList);
VOID FreeTokenLine(IN OUT CHAR16 ***TokenList, IN OUT UINTN *TokenCount);
REFIT_FILE * ReadLinuxOptionsFile(IN CHAR16 *LoaderPath, IN REFIT_VOLUME *Volume);
CHAR16 * GetFirstOptionsFromFile(IN CHAR16 *LoaderPath, IN REFIT_VOLUME *Volume);

#endif

 
