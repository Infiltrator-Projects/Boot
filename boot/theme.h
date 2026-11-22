/* Graphical theme API and image ownership.
 * The background is borrowed from a cache; selection plates are owned.
 * Wordmark composition draws onto its caller-supplied image.
 */

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
#endif
