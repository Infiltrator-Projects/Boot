/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_COMMON_V2_VOLUME_H
#define BOOT_COMMON_V2_VOLUME_H
#include <efi.h>
/* Mount only after the existing EFI drivers have had first refusal.
 * Returns the number of new Simple File System protocols published. */
UINTN BootV2MountVolumes(VOID);
BOOLEAN BootV2OwnsVolume(EFI_HANDLE);
BOOLEAN BootV2HasVolumes(VOID);
/* Called only after BOOT closes its own file handles. A protocol that
 * cannot be safely uninstalled prevents returning from the application. */
EFI_STATUS BootV2Shutdown(VOID);
#endif
