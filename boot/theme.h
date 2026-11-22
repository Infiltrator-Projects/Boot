/* Graphite theme interface. Wallpaper is a module-owned cached image,
 * valid until a later successful resize or graphics teardown. Wordmark draws
 * into a borrowed image and returns its planned lower Y edge. Selection and
 * tile calls allocate images that the caller must release with egFreeImage.
 * None of these functions enumerate or launch operating systems. */
#ifndef INFILTRATOR_BOOT_THEME_H
#define INFILTRATOR_BOOT_THEME_H
#include "global.h"
/* Borrow the size-matched wallpaper; NULL selects the existing UI fallback. */
EG_IMAGE *BootThemeBackground(UINTN Width, UINTN Height);
/* Draw to a borrowed RGBA surface; zero indicates rejected dimensions. */
UINTN BootThemeDrawWordmark(EG_IMAGE *Background);
/* Return caller-owned menu plates; NULL indicates invalid size or OOM. */
EG_IMAGE *BootThemeSelection(UINTN Size);
EG_IMAGE *BootThemeTile(UINTN Size);
/* Caller-owned silver/cyan projection of one transparent utility icon. */
EG_IMAGE *BootThemeTintToolIcon(EG_IMAGE *Original, BOOLEAN Accent);
#endif
