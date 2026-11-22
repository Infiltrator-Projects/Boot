/*
 * BOOT / EFI file protocol structures
 *
 * EFI Simple File System wrapper structures and signatures.
 *
 * The EFI-visible file handle embeds an FSW dnode/stream; its signature
 * guards container recovery but is not a cryptographic authenticity check.
 * Protocol callbacks must maintain EFI_STATUS, position and buffer-size
 * semantics.
 */

#ifndef _FSW_EFI_H_
#define _FSW_EFI_H_

#include "fsw_core.h"
#include "../include/boot_call_wrapper.h"

#ifdef __MAKEWITH_GNUEFI
#define CompareGuid(a, b) CompareGuid(a, b)==0
#endif

#define BOOT_EFI_DISK_IO_PROTOCOL_GUID \
  { \
    0xce345171, 0xba0b, 0x11d2, {0x8e, 0x4f, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b } \
  }

#define BOOT_EFI_BLOCK_IO_PROTOCOL_GUID \
  { \
    0x964e5b21, 0x6459, 0x11d2, {0x8e, 0x39, 0x0, 0xa0, 0xc9, 0x69, 0x72, 0x3b } \
  }

/*
 * FileSystem is embedded in the per-controller state installed on the
 * firmware handle. The container macro recovers that state from a protocol
 * callback. MediaId and LastIOStatus keep Disk I/O tied to the mounted
 * medium.
 */
typedef struct {
    UINT64                      Signature;       

    EFI_FILE_IO_INTERFACE       FileSystem;      

    EFI_HANDLE                  Handle;          
    EFI_DISK_IO                 *DiskIo;         
    UINT32                      MediaId;         
    EFI_STATUS                  LastIOStatus;    

    struct fsw_volume           *vol;            

} FSW_VOLUME_DATA;

#define FSW_VOLUME_DATA_SIGNATURE  EFI_SIGNATURE_32 ('f', 's', 'w', 'V')

#define FSW_VOLUME_FROM_FILE_SYSTEM(a)  CR (a, FSW_VOLUME_DATA, FileSystem, FSW_VOLUME_DATA_SIGNATURE)
/*
 * Each published file protocol encloses its own FSW stream handle. Two opens
 * of the same dnode therefore share metadata while keeping independent
 * positions and cached extents.
 */
typedef struct {
    UINT64                      Signature;       

    EFI_FILE_PROTOCOL           FileHandle;      

    UINT64                       Type;            
    struct fsw_shandle          shand;           

} FSW_FILE_DATA;

#define FSW_EFI_FILE_TYPE_FILE  (0)

#define FSW_EFI_FILE_TYPE_DIR   (1)

#define FSW_FILE_DATA_SIGNATURE    EFI_SIGNATURE_32 ('f', 's', 'w', 'F')

#define FSW_FILE_FROM_FILE_HANDLE(a)  CR (a, FSW_FILE_DATA, FileHandle, FSW_FILE_DATA_SIGNATURE)

/*
 * These helpers write into existing EFI metadata. String destinations must fit
 * the converted name and terminator; cache invalidation is global to the
 * adapter, not tied to one open file.
 */
VOID fsw_efi_decode_time(OUT EFI_TIME *EfiTime, IN UINT32 UnixTime);

UINTN fsw_efi_strsize(struct fsw_string *s);
VOID fsw_efi_strcpy(CHAR16 *Dest, struct fsw_string *src);
VOID EFIAPI fsw_efi_clear_cache(VOID);

#endif
