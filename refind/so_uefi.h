/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_SO_UEFI_H
#define BOOT_SO_UEFI_H
#ifdef __MAKEWITH_GNUEFI
#if defined(__x86_64__) && !defined(GNU_EFI_USE_MS_ABI)
#define GNU_EFI_USE_MS_ABI
#endif
#include <efi.h>
#else
#include "../include/tiano_includes.h"
#endif
UINTN BootSoLoadDirectory(EFI_HANDLE, EFI_FILE_PROTOCOL *, CHAR16 *, BOOLEAN);
BOOLEAN BootSoOwnsParent(EFI_HANDLE);
BOOLEAN BootSoHandleMatches(EFI_HANDLE, const CHAR16 *);
#endif
