/*
 * UEFI absolute/simple-pointer protocol enumeration and cursor state.
 * Pointer hardware is optional; keyboard navigation must remain usable.
 * Opened firmware protocols and cached background images have explicit
 * lifetime requirements when graphics modes or pointer handles change.
 */
 




















#include "pointer.h"
#include "global.h"
#include "screen.h"
#include "icns.h"
#include "../include/refit_call_wrapper.h"
#include "../boot/lib.h"

EFI_HANDLE* APointerHandles = NULL;
EFI_ABSOLUTE_POINTER_PROTOCOL** APointerProtocol = NULL;
EFI_GUID APointerGuid = EFI_ABSOLUTE_POINTER_PROTOCOL_GUID;
UINTN NumAPointerDevices = 0;

EFI_HANDLE* SPointerHandles = NULL;
EFI_SIMPLE_POINTER_PROTOCOL** SPointerProtocol = NULL;
EFI_GUID SPointerGuid = EFI_SIMPLE_POINTER_PROTOCOL_GUID;
UINTN NumSPointerDevices = 0;

BOOLEAN PointerAvailable = FALSE;

UINTN LastXPos = 0, LastYPos = 0;
EG_IMAGE* MouseImage = NULL;
EG_IMAGE* Background = NULL;

POINTER_STATE State;

 
 
 
/*
 * Discovers and opens available pointer protocols. Failure or zero handles
 * must leave the rest of the firmware menu usable without pointer input.
 */
VOID pdInitialize() {
    pdCleanup();  

    if (!(GlobalConfig.EnableMouse || GlobalConfig.EnableTouch)) return;

     
    UINTN NumPointerHandles = 0;
    EFI_STATUS handlestatus = refit_call5_wrapper(BS->LocateHandleBuffer, ByProtocol, &APointerGuid, NULL,
                                                  &NumPointerHandles, &APointerHandles);

    if (!EFI_ERROR(handlestatus)) {
        APointerProtocol = AllocatePool(sizeof(EFI_ABSOLUTE_POINTER_PROTOCOL*) * NumPointerHandles);
        UINTN Index;
        for(Index = 0; Index < NumPointerHandles; Index++) {
             
            EFI_STATUS status = refit_call6_wrapper(BS->OpenProtocol, APointerHandles[Index], &APointerGuid,
                                                    (VOID **) &APointerProtocol[NumAPointerDevices],
                                                    SelfImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
            if (status == EFI_SUCCESS) {
                NumAPointerDevices++; 
            }
        }
    } else {
        GlobalConfig.EnableTouch = FALSE;
    }

     
    NumPointerHandles = 0;
    handlestatus = refit_call5_wrapper(BS->LocateHandleBuffer, ByProtocol, &SPointerGuid, NULL,
                                       &NumPointerHandles, &SPointerHandles);

    if(!EFI_ERROR(handlestatus)) {
        SPointerProtocol = AllocatePool(sizeof(EFI_SIMPLE_POINTER_PROTOCOL*) * NumPointerHandles);
        UINTN Index;
        for(Index = 0; Index < NumPointerHandles; Index++) {
             
            EFI_STATUS status = refit_call6_wrapper(BS->OpenProtocol, SPointerHandles[Index], &SPointerGuid, (VOID **) &SPointerProtocol[NumSPointerDevices], SelfImageHandle, NULL, EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL);
            if (status == EFI_SUCCESS) {
                NumSPointerDevices++; 
            }
        }
    } else {
        GlobalConfig.EnableMouse = FALSE;
    }

    PointerAvailable = (NumAPointerDevices + NumSPointerDevices > 0);

     
    if (PointerAvailable && GlobalConfig.EnableMouse) {
        MouseImage = BuiltinIcon(BUILTIN_ICON_MOUSE);
    }
}

 
 
 
/*
 * Closes opened firmware protocols and releases cursor-related state;
 * subsequent pointer reads require fresh initialisation.
 */
VOID pdCleanup() {
    PointerAvailable = FALSE;
    pdClear();

    if(APointerHandles) {
        UINTN Index;
        for(Index = 0; Index < NumAPointerDevices; Index++) {
            refit_call4_wrapper(BS->CloseProtocol, APointerHandles[Index], &APointerGuid, SelfImageHandle, NULL);
        }
        MyFreePool(APointerHandles);
        APointerHandles = NULL;
    }
    if(APointerProtocol) {
        MyFreePool(APointerProtocol);
        APointerProtocol = NULL;
    }
    if(SPointerHandles) {
        UINTN Index;
        for(Index = 0; Index < NumSPointerDevices; Index++) {
            refit_call4_wrapper(BS->CloseProtocol, SPointerHandles[Index], &SPointerGuid, SelfImageHandle, NULL);
        }
        MyFreePool(SPointerHandles);
        SPointerHandles = NULL;
    }
    if(SPointerProtocol) {
        MyFreePool(SPointerProtocol);
        SPointerProtocol = NULL;
    }
    if(MouseImage) {
        egFreeImage(MouseImage);
        Background = NULL;
    }
    NumAPointerDevices = 0;
    NumSPointerDevices = 0;

    LastXPos = UGAWidth / 2;
    LastYPos = UGAHeight / 2;

    State.X = UGAWidth / 2;
    State.Y = UGAHeight / 2;
    State.Press = FALSE;
    State.Holding = FALSE;
}

 
 
 
BOOLEAN pdAvailable() {
    return PointerAvailable;
}

 
 
 
UINTN pdCount() {
    return NumAPointerDevices + NumSPointerDevices;
}

 
 
 
EFI_EVENT pdWaitEvent(UINTN Index) {
    if(!PointerAvailable || Index >= NumAPointerDevices + NumSPointerDevices) {
        return NULL;
    }

    if(Index >= NumAPointerDevices) {
        return SPointerProtocol[Index - NumAPointerDevices]->WaitForInput;
    }
    return APointerProtocol[Index]->WaitForInput;
}

 
 
 
 
/*
 * Aggregates pointer device reports into screen coordinates; devices have
 * different motion conventions and may fail independently.
 */
EFI_STATUS pdUpdateState() {
#if defined(EFI32) && defined(__MAKEWITH_GNUEFI)
    return EFI_NOT_READY;
#else
    if(!PointerAvailable) {
        return EFI_NOT_READY;
    }

    EFI_STATUS Status = EFI_NOT_READY;
    EFI_ABSOLUTE_POINTER_STATE APointerState;
    EFI_SIMPLE_POINTER_STATE SPointerState;
    BOOLEAN LastHolding = State.Holding;

    UINTN Index;
    for(Index = 0; Index < NumAPointerDevices; Index++) {
        EFI_STATUS PointerStatus = refit_call2_wrapper(APointerProtocol[Index]->GetState, APointerProtocol[Index], &APointerState);
         
        if(!EFI_ERROR(PointerStatus) && EFI_ERROR(Status)) {
            Status = EFI_SUCCESS;

#ifdef EFI32
            State.X = (UINTN)DivU64x64Remainder(APointerState.CurrentX * UGAWidth, APointerProtocol[Index]->Mode->AbsoluteMaxX, NULL);
            State.Y = (UINTN)DivU64x64Remainder(APointerState.CurrentY * UGAHeight, APointerProtocol[Index]->Mode->AbsoluteMaxY, NULL);
#else
            State.X = (APointerState.CurrentX * UGAWidth) / APointerProtocol[Index]->Mode->AbsoluteMaxX;
            State.Y = (APointerState.CurrentY * UGAHeight) / APointerProtocol[Index]->Mode->AbsoluteMaxY;
#endif
            State.Holding = (APointerState.ActiveButtons & EFI_ABSP_TouchActive);
        }
    }
    for(Index = 0; Index < NumSPointerDevices; Index++) {
        EFI_STATUS PointerStatus = refit_call2_wrapper(SPointerProtocol[Index]->GetState, SPointerProtocol[Index], &SPointerState);
         
        if(!EFI_ERROR(PointerStatus) && EFI_ERROR(Status)) {
            Status = EFI_SUCCESS;

            INT32 TargetX = 0;
            INT32 TargetY = 0;

#ifdef EFI32
	    TargetX = State.X + (INTN)DivS64x64Remainder(SPointerState.RelativeMovementX * GlobalConfig.MouseSpeed, SPointerProtocol[Index]->Mode->ResolutionX, NULL);
            TargetY = State.Y + (INTN)DivS64x64Remainder(SPointerState.RelativeMovementY * GlobalConfig.MouseSpeed, SPointerProtocol[Index]->Mode->ResolutionY, NULL);
#else
            TargetX = State.X + SPointerState.RelativeMovementX * GlobalConfig.MouseSpeed / SPointerProtocol[Index]->Mode->ResolutionX;
            TargetY = State.Y + SPointerState.RelativeMovementY * GlobalConfig.MouseSpeed / SPointerProtocol[Index]->Mode->ResolutionY;
#endif

            if(TargetX < 0) {
                State.X = 0;
            } else if(TargetX >= UGAWidth) {
                State.X = UGAWidth - 1;
            } else {
                State.X = TargetX;
            }

            if(TargetY < 0) {
                State.Y = 0;
            } else if(TargetY >= UGAHeight) {
                State.Y = UGAHeight - 1;
            } else { 
                State.Y = TargetY;
            }

            State.Holding = SPointerState.LeftButton;
        }
    }

    State.Press = (LastHolding && !State.Holding);

    return Status;
#endif
}

 
 
 
POINTER_STATE pdGetState() {
    return State;
}

 
 
 
/*
 * Preserves the under-cursor background for later restoration. This is a
 * visual operation and must not influence boot entry selection.
 */
VOID pdDraw() {
    if(Background) {
        egFreeImage(Background);
        Background = NULL;
    }
    if(MouseImage) {
        UINTN Width = MouseImage->Width;
        UINTN Height = MouseImage->Height;

        if(State.X + Width > UGAWidth) {
            Width = UGAWidth - State.X;
        }
        if(State.Y + Height > UGAHeight) {
            Height = UGAHeight - State.Y;
        }

        Background = egCopyScreenArea(State.X, State.Y, Width, Height);
        if(Background) {
            BltImageCompositeBadge(Background, MouseImage, NULL, State.X, State.Y);
        }
    }
    LastXPos = State.X;
    LastYPos = State.Y;
}

 
 
 
VOID pdClear() {
    if (Background) {
        egDrawImage(Background, LastXPos, LastYPos);
        egFreeImage(Background);
        Background = NULL;
    }
}
