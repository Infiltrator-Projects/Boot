/* BOOT graphite luxury theme. Draws only presentation; never boot state. */
#ifndef INFILTRATOR_BOOT_THEME_H
#define INFILTRATOR_BOOT_THEME_H
#include "global.h"
EG_IMAGE *BootThemeBackground(UINTN Width, UINTN Height);
UINTN BootThemeDrawWordmark(EG_IMAGE *Background);
EG_IMAGE *BootThemeSelection(UINTN Size);
EG_IMAGE *BootThemeTile(UINTN Size);
#endif
