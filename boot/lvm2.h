/*
 * BOOT / read-only native LVM2 block-volume discovery
 *
 * Returns TRUE when at least one validated linear LVM2 logical volume is
 * published as an EFI Block/Disk I/O handle. Non-LVM machines are a no-op.
 */
#ifndef BOOT_LVM2_H
#define BOOT_LVM2_H
#include "global.h"
BOOLEAN LvmDiscoverVolumes(VOID);
#endif
