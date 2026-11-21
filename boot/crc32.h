/*
 * CRC-32 calculation interface for metadata integrity checks, notably GPT.
 * CRC detects accidental changes, not malicious replacement or code signing.
 * Caller supplies the exact byte range and expected stored checksum.
 */
 








































 



#ifndef __CRC32_H_
#define __CRC32_H_

#ifdef __MAKEWITH_GNUEFI
#include "efi.h"
#include "efilib.h"
#else
#include "../include/tiano_includes.h"
#endif

UINT32 crc32(UINT32 crc, const VOID *buf, UINTN size);

#endif