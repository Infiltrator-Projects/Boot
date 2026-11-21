 


































 







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
