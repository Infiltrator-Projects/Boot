/*
 * Pass zero for a new CRC-32 calculation or the previous return value to
 * extend one across successive buffers. size is measured in bytes. GPT header
 * and entry-array checks start separate calculations.
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