/* Configuration parser types, defaults and public operations.
 * Scan exclusions affect menu visibility; parsing allocates strings
 * that must be released by the documented token helpers.
 */

#ifndef __CONFIG_H_
#define __CONFIG_H_

#ifdef __MAKEWITH_GNUEFI
#include "efi.h"
#else
#include "../include/tiano_includes.h"
#endif
/*
 * Configuration text comes from a writable EFI-visible volume. Encoding and
 * cursor units follow REFIT_FILE's byte and CHAR16 boundaries. Configuration
 * governs visibility and preferences; it is not an execution trust grant.
 */
#include "global.h"
/*
 * Both cursor pairs describe the same file buffer. BufferSize is in bytes,
 * while advancing a CHAR16 pointer consumes two bytes. ReadLine selects the
 * pair using the detected encoding and stops at its corresponding end
 * pointer.
 */
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

/*
 * ReadFile allocates and fills REFIT_FILE from an EFI file, reporting the
 * byte length through size. The token parser returns independently
 * allocated UTF-16 strings; FreeTokenLine releases each string and its
 * pointer array.
 */
EFI_STATUS ReadFile(IN EFI_FILE_HANDLE BaseDir, CHAR16 *FileName, REFIT_FILE *File, UINTN *size);
/*
 * ReadConfig replaces runtime preferences in GlobalConfig;
 * ScanUserConfigured builds loader candidates from the selected
 * configuration file. Neither call establishes an image's authenticity or
 * a writable EFI partition.
 */
VOID ReadConfig(CHAR16 *FileName);
VOID ScanUserConfigured(CHAR16 *FileName);
UINTN ReadTokenLine(IN REFIT_FILE *File, OUT CHAR16 ***TokenList);
VOID FreeTokenLine(IN OUT CHAR16 ***TokenList, IN OUT UINTN *TokenCount);
/*
 * ReadLinuxOptionsFile and GetFirstOptionsFromFile inspect the adjacent
 * Linux options file. The resulting REFIT_FILE wrapper and returned
 * command-line string have different lifetimes; options are borrowed from
 * the specified volume only during reading.
 */
REFIT_FILE * ReadLinuxOptionsFile(IN CHAR16 *LoaderPath, IN REFIT_VOLUME *Volume);
CHAR16 * GetFirstOptionsFromFile(IN CHAR16 *LoaderPath, IN REFIT_VOLUME *Volume);

#endif
