/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_COMMON_V2_UEFI_TRUST_H
#define BOOT_COMMON_V2_UEFI_TRUST_H
#include <efi.h>
#include <stddef.h>
/* Authenticate an exact ELF SHA-256 digest using explicitly enrolled
 * firmware db hash entries, with dbx taking precedence. Never trusts a
 * companion checksum, an unverified MOK file, or Setup Mode variables. */
int BootV2Authenticate(VOID *,const void *,size_t);
#endif
