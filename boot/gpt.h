/* GPT metadata structures and inspection API.
 * On-disk lengths, UUIDs and table checksums are parsed from raw blocks.
 * The returned partition records are descriptive, not mounted volumes.
 */

#ifndef __GPT_H_
#define __GPT_H_

#include "global.h"

#ifdef __MAKEWITH_GNUEFI
#include "efi.h"
#include "efilib.h"
#else
#include "../include/tiano_includes.h"
#endif

#pragma pack(1)
typedef struct {
   UINT8   flags;
   UINT8   start_chs[3];
   UINT8   type;
   UINT8   end_chs[3];
   UINT32  start_lba;
   UINT32  size;
} MBR_PART_INFO;

typedef struct {
   UINT8            code[440];
   UINT32           diskSignature;
   UINT16           nulls;
   MBR_PART_INFO    partitions[4];
   UINT16           MBRSignature;
} MBR_RECORD;

typedef struct {
   UINT64  signature;
   UINT32  spec_revision;
   UINT32  header_size;
   /*
    * The checksum covers the header with this field set to zero. The
    * independent entry_crc32 covers the partition entry array and uses the
    * geometry specified later in this header.
    */
   UINT32  header_crc32;
   UINT32  reserved;
   UINT64  header_lba;
   UINT64  alternate_header_lba;
   UINT64  first_usable_lba;
   UINT64  last_usable_lba;
   UINT8   disk_guid[16];
   UINT64  entry_lba;
   UINT32  entry_count;
   UINT32  entry_size;
   UINT32  entry_crc32;
   UINT8   reserved2[420];
} GPT_HEADER;

typedef struct {
   UINT8   type_guid[16];
   UINT8   partition_guid[16];
   UINT64  start_lba;
   UINT64  end_lba;
   UINT64  attributes;
   CHAR16  name[36];
} GPT_ENTRY;

typedef struct _gpt_data {
   MBR_RECORD         *ProtectiveMBR;
   GPT_HEADER         *Header;
   GPT_ENTRY          *Entries;
   struct _gpt_data   *NextEntry;
} GPT_DATA;

#pragma pack(0)

/*
 * ReadGptData returns metadata from a selected volume after GPT structural
 * checks. Its allocated header/entry records belong to the GPT_DATA
 * object; ClearGptData releases that ownership.
 */
VOID ClearGptData(GPT_DATA *Data);
EFI_STATUS ReadGptData(REFIT_VOLUME *Volume, GPT_DATA **Data);

/*
 * Partition-table discovery and cached GUID lookup have scan-generation
 * lifetimes. A returned entry pointer must not be kept across
 * ForgetPartitionTables; checksum validity is not loader authenticity.
 */
GPT_ENTRY * FindPartWithGuid(EFI_GUID *Guid);
VOID ForgetPartitionTables(VOID);
VOID AddPartitionTable(REFIT_VOLUME *Volume);

#endif
