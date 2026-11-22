/*
 * BOOT / Pointer input API
 *
 * UEFI pointer device discovery and cursor interface.
 *
 * Absolute and simple pointer devices are optional inputs. Keyboard
 * navigation does not depend on cursor availability.
 */

#ifndef __BOOT_POINTERDEVICE_H_
#define __BOOT_POINTERDEVICE_H_

#ifdef __MAKEWITH_GNUEFI
#include "efi.h"
#include "efilib.h"
#else
#include "../include/tiano_includes.h"
#endif

#ifndef _EFI_POINT_H
#include "../EfiLib/AbsolutePointer.h"
#endif

typedef struct PointerStateStruct {
    UINTN X, Y;
    /*
     * Holding describes the current touch or button level. Press is set on
     * its transition from held to released. Menu activation relies on this
     * edge to avoid repeating an action on every sample.
     */
    BOOLEAN Press;
    BOOLEAN Holding;
} POINTER_STATE;

/*
 * Pointer initialisation discovers firmware device protocols and allocates
 * cursor assets. pdCleanup releases those resources; the absence of
 * pointers must not affect keyboard boot-menu input.
 */
VOID pdInitialize();
VOID pdCleanup();
BOOLEAN pdAvailable();
UINTN pdCount();
/*
 * pdCount limits valid event indices. pdUpdateState normalises hardware
 * coordinates, and pdGetState returns a snapshot with a release-edge Press
 * flag; events are firmware-owned.
 */
EFI_EVENT pdWaitEvent(IN UINTN Index);
EFI_STATUS pdUpdateState();
POINTER_STATE pdGetState();

/*
 * Pointer painting borrows the current screen background and may cache
 * covered pixels. Clearing/redrawing the cursor is presentation-only and
 * must track graphics mode transitions.
 */
VOID pdDraw();
VOID pdClear();

#endif
