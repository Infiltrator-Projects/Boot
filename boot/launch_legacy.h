/*
 * Apple-compatible legacy startup prepares the disk and starts a firmware
 * loader image. The UEFI legacy route dispatches a BDS boot option through
 * the legacy BIOS protocol. Scan functions populate the entry type that
 * selects the corresponding launch route.
 */
 


































 







#ifndef __LAUNCH_LEGACY_H
#define __LAUNCH_LEGACY_H

#include "global.h"

VOID StartLegacy(IN LEGACY_ENTRY *Entry, IN CHAR16 *SelectionName);
VOID StartLegacyUEFI(LEGACY_ENTRY *Entry, CHAR16 *SelectionName);
VOID ScanLegacyDisc(VOID);
VOID ScanLegacyInternal(VOID);
VOID ScanLegacyExternal(VOID);
VOID FindLegacyBootType(VOID);
VOID WarnIfLegacyProblems(VOID);

#endif
