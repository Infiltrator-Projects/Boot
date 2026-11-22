/* CRC-32 checksum helper interface.
 * Uses the reflected CRC-32 polynomial for binary metadata.
 * A checksum is a corruption check, not executable authentication.
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