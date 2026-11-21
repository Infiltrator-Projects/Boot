/*
 * Optional EFI absolute/simple-pointer device abstraction. Keyboard-only
 * selection remains required when no compatible pointer protocol is present.
 * Pointer handle closure and cached graphics release are lifecycle contracts.
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
    BOOLEAN Press;
    BOOLEAN Holding;
} POINTER_STATE;

VOID pdInitialize();
VOID pdCleanup();
BOOLEAN pdAvailable();
UINTN pdCount();
EFI_EVENT pdWaitEvent(IN UINTN Index);
EFI_STATUS pdUpdateState();
POINTER_STATE pdGetState();

VOID pdDraw();
VOID pdClear();

#endif

 
