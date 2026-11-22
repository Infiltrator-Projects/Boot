/*
 * UEFI graphical console/GOP mode selection and framebuffer drawing.
 * Firmware modes may be absent, constrained or behave differently across
 * platforms. Screen resolution and saved pixel areas must be bounds checked;
 * text-mode fallback is required for a usable boot menu.
 * GOP mode changes may invalidate cached dimensions and saved pixel regions.
 * Rendering failure must remain a presentation failure, not silently change
 * the selected boot target or erase independently discovered loader entries.
 */

#include "libegint.h"
#include "../boot/screen.h"
#include "../boot/lib.h"
#include "../boot/mystrings.h"
#include "../include/refit_call_wrapper.h"
#include "libeg.h"
#include "log.h"
#include "../include/Handle.h"

#include <efiUgaDraw.h>
#include <efiConsoleControl.h>

#ifndef __MAKEWITH_GNUEFI
#define LibLocateProtocol EfiLibLocateProtocol
#else
#include <efilib.h>
#endif

static EFI_GUID ConsoleControlProtocolGuid = EFI_CONSOLE_CONTROL_PROTOCOL_GUID;
static EFI_CONSOLE_CONTROL_PROTOCOL *ConsoleControl = NULL;

static EFI_GUID UgaDrawProtocolGuid = EFI_UGA_DRAW_PROTOCOL_GUID;
static EFI_UGA_DRAW_PROTOCOL *UgaDraw = NULL;

static EFI_GUID GraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *GraphicsOutput = NULL;

static BOOLEAN egHasGraphics  = FALSE;
static UINTN   egScreenWidth  = 800;
static UINTN   egScreenHeight = 600;

/**
 * On GOP firmware the current candidate is replaced only when both pixel
 * dimensions increase. A wider, shorter mode is skipped and the result can depend
 * on enumeration order. The older UGA interface simply supplies the geometry of
 * its active mode.
 */
VOID egSetMaxResolution(VOID) {
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;

    EFI_STATUS   Status   = EFI_UNSUPPORTED;
    UINTN        Zero     = 0;
    UINT32       Width    = 0;
    UINT32       Height   = 0;
    UINT32       BestMode = 0;
    UINT32       Mode;
    UINT32       UGAWidth, UGAHeight, UGADepth, UGARefreshRate;
    UINTN        SizeOfInfo;
    BOOLEAN      Result;

    if (GraphicsOutput == NULL) {

        Status = refit_call5_wrapper(UgaDraw->GetMode,
                                     UgaDraw,
                                     &UGAWidth,
                                     &UGAHeight,
                                     &UGADepth,
                                     &UGARefreshRate);
        egScreenWidth = GlobalConfig.RequestedScreenWidth = UGAWidth;
        egScreenHeight = GlobalConfig.RequestedScreenHeight = UGAHeight;
        return;
    }

    for (Mode = 0; Mode < GraphicsOutput->Mode->MaxMode; Mode++) {
        Status = refit_call4_wrapper(GraphicsOutput->QueryMode,
                                     GraphicsOutput,
                                     Mode,
                                     &SizeOfInfo,
                                     &Info);
        if (!EFI_ERROR(Status)) {
            if ((Width < Info->HorizontalResolution) && (Height < Info->VerticalResolution)) {
                Width = Info->HorizontalResolution;
                Height = Info->VerticalResolution;
                BestMode = Mode;
            }
        }  
    }  

    if (BestMode == GraphicsOutput->Mode->Mode) {
        egScreenWidth = GlobalConfig.RequestedScreenWidth = GraphicsOutput->Mode->Info->HorizontalResolution;
        egScreenHeight = GlobalConfig.RequestedScreenHeight = GraphicsOutput->Mode->Info->VerticalResolution;
        Status = EFI_SUCCESS;
    } else {  
        Result = egSetScreenSize((UINTN*) &BestMode, &Zero);
        if (Result) {
            egScreenWidth = GlobalConfig.RequestedScreenWidth = Width;
            egScreenHeight = GlobalConfig.RequestedScreenHeight = Height;
        } else {

            BestMode = GraphicsOutput->Mode->Mode;
            Zero = 0;
            Result = egSetScreenSize((UINTN*) &BestMode, &Zero);
        }  
    }  

    return;
}  

/*
 * Prefer GOP's active mode and fall back to UGA's GetMode response.
 * Protocol presence does not guarantee the mode query succeeds; a
 * failed UGA query clears the cached protocol pointer.
 */
static VOID egDetermineScreenSize(VOID) {
    EFI_STATUS Status = EFI_SUCCESS;
    UINT32 UGAWidth, UGAHeight, UGADepth, UGARefreshRate;

    egHasGraphics = FALSE;
    if (GraphicsOutput != NULL) {
        egScreenWidth = GraphicsOutput->Mode->Info->HorizontalResolution;
        egScreenHeight = GraphicsOutput->Mode->Info->VerticalResolution;
        egHasGraphics = TRUE;
    } else if (UgaDraw != NULL) {
        Status = refit_call5_wrapper(UgaDraw->GetMode, UgaDraw, &UGAWidth, &UGAHeight, &UGADepth, &UGARefreshRate);
        if (EFI_ERROR(Status)) {
            UgaDraw = NULL;    
        } else {
            egScreenWidth = UGAWidth;
            egScreenHeight = UGAHeight;
            egHasGraphics = TRUE;
        }
    }
}  

VOID egGetScreenSize(OUT UINTN *ScreenWidth, OUT UINTN *ScreenHeight)
{
    egDetermineScreenSize();

    if (ScreenWidth != NULL)
        *ScreenWidth = egScreenWidth;
    if (ScreenHeight != NULL)
        *ScreenHeight = egScreenHeight;
}  

/*
 * Discover optional console-control, UGA and GOP protocols before
 * selecting the initial display dimensions. A requested maximum mode
 * is applied through firmware, not by resizing BOOT's image buffers.
 */
VOID egInitScreen(VOID)
{
    EFI_STATUS Status = EFI_SUCCESS;

    Status = LibLocateProtocol(&ConsoleControlProtocolGuid, (VOID **) &ConsoleControl);
    if (EFI_ERROR(Status))
        ConsoleControl = NULL;

    Status = LibLocateProtocol(&UgaDrawProtocolGuid, (VOID **) &UgaDraw);
    if (EFI_ERROR(Status))
        UgaDraw = NULL;

    Status = LibLocateProtocol(&GraphicsOutputProtocolGuid, (VOID **) &GraphicsOutput);
    if (EFI_ERROR(Status))
        GraphicsOutput = NULL;

    if ((GlobalConfig.RequestedScreenHeight == MAX_RES_CODE) &&
            (GlobalConfig.RequestedScreenWidth == MAX_RES_CODE)) {
        egSetMaxResolution();
    }
    egDetermineScreenSize();
}  

/* Query one GOP mode index supplied through ModeWidth, replacing the two
 * outputs with its pixel dimensions only on success. QueryMode returns an
 * allocation through Info; this routine does not visibly release it, so
 * repeated mode inspection has an EFI pool-lifetime consideration.
 */
BOOLEAN egGetResFromMode(UINTN *ModeWidth, UINTN *Height) {
    UINTN                                 Size;
    EFI_STATUS                            Status;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info = NULL;

    if ((ModeWidth != NULL) && (Height != NULL) && GraphicsOutput) {
        Status = refit_call4_wrapper(GraphicsOutput->QueryMode, GraphicsOutput,
                                     *ModeWidth, &Size, &Info);
        if (!EFI_ERROR(Status) && (Info != NULL)) {
            *ModeWidth = Info->HorizontalResolution;
            *Height = Info->VerticalResolution;
            return TRUE;
        }
    }
    return FALSE;
}  

/*
 * Selects a supported firmware graphics mode. A requested resolution is a
 * preference, not proof that the firmware exposes an identical mode.
 */
BOOLEAN egSetScreenSize(IN OUT UINTN *ScreenWidth, IN OUT UINTN *ScreenHeight) {
    EFI_STATUS                            Status = EFI_SUCCESS;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *Info;
    UINTN                                 Size;
    UINT32                                ModeNum = 0, CurrentModeNum;
    UINT32                                UGAWidth, UGAHeight, UGADepth, UGARefreshRate;
    BOOLEAN                               ModeSet = FALSE;

    if ((ScreenWidth == NULL) || (ScreenHeight == NULL)) {
        LOG(1, LOG_LINE_NORMAL, L"Error: ScreenWidth or ScreenHeight is NULL in egSetScreenSize()!");
        return FALSE;
    }

    if (GraphicsOutput != NULL) {  
        CurrentModeNum = GraphicsOutput->Mode->Mode;

        if (*ScreenHeight == 0) {  
            ModeNum = (UINT32) *ScreenWidth;
            if (ModeNum == CurrentModeNum) {
                ModeSet = TRUE;
            } else if (egGetResFromMode(ScreenWidth, ScreenHeight) &&
                       (refit_call2_wrapper(GraphicsOutput->SetMode,
                                            GraphicsOutput, ModeNum) == EFI_SUCCESS)) {
                LOG(2, LOG_LINE_NORMAL, L"Setting GOP mode to %d", ModeNum);
                ModeSet = TRUE;
            }

        } else {

            do {
                Status = refit_call4_wrapper(GraphicsOutput->QueryMode, GraphicsOutput,
                                             ModeNum, &Size, &Info);
                if ((Status == EFI_SUCCESS) && (Size >= sizeof(*Info) && (Info != NULL)) &&
                    (Info->HorizontalResolution == *ScreenWidth) &&
                    (Info->VerticalResolution == *ScreenHeight) &&
                    ((ModeNum == CurrentModeNum) ||
                     (refit_call2_wrapper(GraphicsOutput->SetMode,
                                          GraphicsOutput, ModeNum) == EFI_SUCCESS))) {
                    LOG(2, LOG_LINE_NORMAL, L"Setting GOP mode to %d (%dx%d)",
                        ModeNum, *ScreenWidth, *ScreenHeight);
                    ModeSet = TRUE;
                }  
            } while ((++ModeNum < GraphicsOutput->Mode->MaxMode) && !ModeSet);
        }  

        if (ModeSet) {
            egScreenWidth = *ScreenWidth;
            egScreenHeight = *ScreenHeight;
        } else { 
            SwitchToText(FALSE);
            Print(L"Error setting graphics mode %d x %d; using default mode!\nAvailable modes are:\n",
                  *ScreenWidth, *ScreenHeight);
            LOG(1, LOG_LINE_NORMAL, L"Error setting graphics mode %d x %d; using default mode!",
                *ScreenWidth, *ScreenHeight)
            LOG(1, LOG_LINE_NORMAL, L"Available modes are:");
            ModeNum = 0;
            do {
                Status = refit_call4_wrapper(GraphicsOutput->QueryMode,
                                             GraphicsOutput, ModeNum, &Size, &Info);
                if (!EFI_ERROR(Status) && (Info != NULL)) {
                    Print(L"Mode %d: %d x %d\n", ModeNum,
                          Info->HorizontalResolution, Info->VerticalResolution);
                    LOG(1, LOG_LINE_NORMAL, L"  Mode %d: %d x %d", ModeNum,
                        Info->HorizontalResolution, Info->VerticalResolution);
                    if (ModeNum == CurrentModeNum) {
                        egScreenWidth = Info->HorizontalResolution;
                        egScreenHeight = Info->VerticalResolution;
                    }  
                }  
            } while (++ModeNum < GraphicsOutput->Mode->MaxMode);
            PauseForKey();
            SwitchToGraphics();
        }  

    } else if ((UgaDraw != NULL) && (*ScreenHeight > 0)) {  

        Status = refit_call5_wrapper(UgaDraw->GetMode, UgaDraw, &UGAWidth,
                                     &UGAHeight, &UGADepth, &UGARefreshRate);
        LOG(1, LOG_LINE_NORMAL, L"Setting UGA Draw mode to %d x %d", *ScreenWidth, *ScreenHeight);
        Status = refit_call5_wrapper(UgaDraw->SetMode, UgaDraw, *ScreenWidth,
                                     *ScreenHeight, UGADepth, UGARefreshRate);
        if (!EFI_ERROR(Status)) {
            egScreenWidth = *ScreenWidth;
            egScreenHeight = *ScreenHeight;
            ModeSet = TRUE;
        } else {

            Print(L"Error setting graphics mode %d x %d; unsupported mode!\n",
                  *ScreenWidth, *ScreenHeight);
            LOG(1, LOG_LINE_NORMAL, L"Error setting graphics mode %d x %d; unsupported mode!",
                *ScreenWidth, *ScreenHeight);
        }  
    }  

    return (ModeSet);
}  

/*
 * A failed firmware text-mode switch invokes the diagnostic text
 * path and enumerates reported console modes. The return value
 * indicates an actual successful change, not that the request was valid.
 */
BOOLEAN egSetTextMode(UINT32 RequestedMode) {
    UINTN         i = 0, Width, Height;
    EFI_STATUS    Status;
    BOOLEAN       ChangedIt = FALSE;

    if ((RequestedMode != DONT_CHANGE_TEXT_MODE) && (RequestedMode != ST->ConOut->Mode->Mode)) {
        LOG(1, LOG_LINE_NORMAL, L"Setting text mode to %d", RequestedMode);
        Status = refit_call2_wrapper(ST->ConOut->SetMode, ST->ConOut, RequestedMode);
        if (Status == EFI_SUCCESS) {
            ChangedIt = TRUE;
        } else {
            SwitchToText(FALSE);
            Print(L"\nError setting text mode %d; available modes are:\n", RequestedMode);
            LOG(1, LOG_LINE_NORMAL, L"Error setting text mode %d; available modes are:", RequestedMode);
            do {
                Status = refit_call4_wrapper(ST->ConOut->QueryMode, ST->ConOut, i, &Width, &Height);
                if (Status == EFI_SUCCESS) {
                    Print(L"Mode %d: %d x %d\n", i, Width, Height);
                    LOG(1, LOG_LINE_NORMAL, L"  Mode %d: %d x %d", i, Width, Height);
                }
            } while (++i < ST->ConOut->Mode->MaxMode);
            Print(L"Mode %d: Use default mode\n", DONT_CHANGE_TEXT_MODE);
            LOG(1, LOG_LINE_NORMAL, L"  Mode %d: Use default mode", DONT_CHANGE_TEXT_MODE);

            PauseForKey();
            SwitchToGraphics();
        }  
    }  
    return ChangedIt;
}  

/*
 * Return an allocated UTF-16 description of the selected graphics or
 * text path. The caller owns this string; format text is diagnostic
 * and does not attest graphics protocol correctness.
 */
CHAR16 * egScreenDescription(VOID)
{
    CHAR16 *GraphicsInfo = NULL, *TextInfo;

    if (egHasGraphics) {
        if (GraphicsOutput != NULL) {
            GraphicsInfo = PoolPrint(L"Graphics Output (UEFI), %dx%d", egScreenWidth, egScreenHeight);
        } else if (UgaDraw != NULL) {
            GraphicsInfo = PoolPrint(L"UGA Draw (EFI 1.10), %dx%d", egScreenWidth, egScreenHeight);
        } else {
            MyFreePool(GraphicsInfo);
            return StrDuplicate(L"Internal Error");
        }
        if (!AllowGraphicsMode) {  
            TextInfo = PoolPrint(L"(in %dx%d text mode)", ConWidth, ConHeight);
            MergeStrings(&GraphicsInfo, TextInfo, L' ');
            MyFreePool(TextInfo);
        }
    } else {
        GraphicsInfo = PoolPrint(L"Text-only console, %dx%d", ConWidth, ConHeight);
    }
    return GraphicsInfo;
}

BOOLEAN egHasGraphicsMode(VOID)
{
    return egHasGraphics;
}

BOOLEAN egIsGraphicsModeEnabled(VOID)
{
    EFI_CONSOLE_CONTROL_SCREEN_MODE CurrentMode;

    if (ConsoleControl != NULL) {
        refit_call4_wrapper(ConsoleControl->GetMode, ConsoleControl, &CurrentMode, NULL, NULL);
        return (CurrentMode == EfiConsoleControlScreenGraphics) ? TRUE : FALSE;
    }

    return FALSE;
}

/*
 * Use the optional console-control protocol to change the text/graphics
 * screen mode only when it differs. Firmware status from GetMode and
 * SetMode is currently ignored, so the call is not a success guarantee.
 */
VOID egSetGraphicsModeEnabled(IN BOOLEAN Enable)
{
    EFI_CONSOLE_CONTROL_SCREEN_MODE CurrentMode;
    EFI_CONSOLE_CONTROL_SCREEN_MODE NewMode;

    if (ConsoleControl != NULL) {
        refit_call4_wrapper(ConsoleControl->GetMode, ConsoleControl, &CurrentMode, NULL, NULL);

        NewMode = Enable ? EfiConsoleControlScreenGraphics
                         : EfiConsoleControlScreenText;
        if (CurrentMode != NewMode)
            refit_call2_wrapper(ConsoleControl->SetMode, ConsoleControl, NewMode);
    }
}

VOID egClearScreen(IN EG_PIXEL *Color)
{
    EFI_UGA_PIXEL FillColor;

    if (!egHasGraphics)
        return;

    if (Color != NULL) {
        FillColor.Red   = Color->r;
        FillColor.Green = Color->g;
        FillColor.Blue  = Color->b;
    } else {
        FillColor.Red   = 0x0;
        FillColor.Green = 0x0;
        FillColor.Blue  = 0x0;
    }
    FillColor.Reserved = 0;

    if (GraphicsOutput != NULL) {

        refit_call10_wrapper(GraphicsOutput->Blt, GraphicsOutput, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)&FillColor, EfiBltVideoFill,
                             0, 0, 0, 0, egScreenWidth, egScreenHeight, 0);
    } else if (UgaDraw != NULL) {
        refit_call10_wrapper(UgaDraw->Blt, UgaDraw, &FillColor, EfiUgaVideoFill, 0, 0, 0, 0, egScreenWidth, egScreenHeight, 0);
    }
}

/*
 * Blits an image to the graphics output. Destination coordinates and visible
 * region must stay within the active GOP framebuffer geometry.
 */
VOID egDrawImage(IN EG_IMAGE *Image, IN UINTN ScreenPosX, IN UINTN ScreenPosY)
{
    EG_IMAGE *CompImage = NULL;

    if (!egHasGraphics || ((ScreenPosX + Image->Width) > egScreenWidth) ||
        ((ScreenPosY + Image->Height) > egScreenHeight) ||
        (ScreenPosX > egScreenWidth) || (ScreenPosY > egScreenHeight))
            return;

    if ((GlobalConfig.ScreenBackground == NULL) || ((Image->Width == egScreenWidth) &&
        (Image->Height == egScreenHeight))) {
       CompImage = Image;
    } else if (GlobalConfig.ScreenBackground == Image) {
       CompImage = GlobalConfig.ScreenBackground;
    } else {
       CompImage = egCropImage(GlobalConfig.ScreenBackground, ScreenPosX, ScreenPosY,
                               Image->Width, Image->Height);
       if (CompImage == NULL) {
          Print(L"Error! Can't crop image in egDrawImage()!\n");
          return;
       }
       egComposeImage(CompImage, Image, 0, 0);
    }

    if (GraphicsOutput != NULL) {
       refit_call10_wrapper(GraphicsOutput->Blt, GraphicsOutput,
                            (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)CompImage->PixelData,
                            EfiBltBufferToVideo, 0, 0, ScreenPosX, ScreenPosY, CompImage->Width,
                            CompImage->Height, 0);
    } else if (UgaDraw != NULL) {
       refit_call10_wrapper(UgaDraw->Blt, UgaDraw, (EFI_UGA_PIXEL *)CompImage->PixelData,
                            EfiUgaBltBufferToVideo, 0, 0, ScreenPosX, ScreenPosY,
                            CompImage->Width, CompImage->Height, 0);
    }
    if ((CompImage != GlobalConfig.ScreenBackground) && (CompImage != Image))
       egFreeImage(CompImage);
}  

/*
 * Crop a private background rectangle before compositing menu icon and badge.
 * The original ScreenBackground remains shared. A failed crop suppresses
 * this paint, but the supplied coordinates must already describe a valid
 * source rectangle; the crop's arithmetic is not independently overflow-safe.
 */
VOID egDrawImageWithTransparency(EG_IMAGE *Image, EG_IMAGE *BadgeImage,
                                 UINTN XPos, UINTN YPos, UINTN Width, UINTN Height) {
    EG_IMAGE *Background;

    Background = egCropImage(GlobalConfig.ScreenBackground, XPos, YPos, Width, Height);
    if (Background != NULL) {
        BltImageCompositeBadge(Background, Image, BadgeImage, XPos, YPos);
        egFreeImage(Background);
    }
}  

/* Blit a bounded subsection of a borrowed image into the current graphics
 * mode. Source coordinates are clipped before GOP/UGA transfer; Delta keeps
 * the original source-row stride rather than repacking the selected columns.
 * EFI blit status is not returned to the higher-level presentation caller.
 */
VOID egDrawImageArea(IN EG_IMAGE *Image,
                     IN UINTN AreaPosX, IN UINTN AreaPosY,
                     IN UINTN AreaWidth, IN UINTN AreaHeight,
                     IN UINTN ScreenPosX, IN UINTN ScreenPosY)
{
    if (!egHasGraphics)
        return;
    /*
     * Clipping changes the width transferred but does not repack the source
     * rows. Both BLT protocols need the original image width in bytes as
     * Delta to reach the same column on the next row.
     */
    egRestrictImageArea(Image, AreaPosX, AreaPosY, &AreaWidth, &AreaHeight);
    if (AreaWidth == 0)
        return;

    if (GraphicsOutput != NULL) {
        refit_call10_wrapper(GraphicsOutput->Blt, GraphicsOutput, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)Image->PixelData,
                             EfiBltBufferToVideo, AreaPosX, AreaPosY, ScreenPosX, ScreenPosY, AreaWidth, AreaHeight,
                             Image->Width * 4);
    } else if (UgaDraw != NULL) {
        refit_call10_wrapper(UgaDraw->Blt, UgaDraw, (EFI_UGA_PIXEL *)Image->PixelData, EfiUgaBltBufferToVideo,
                             AreaPosX, AreaPosY, ScreenPosX, ScreenPosY, AreaWidth, AreaHeight, Image->Width * 4);
    }
}

/*
 * Render a temporary notification image at a persistent vertical
 * position. The default placement stacks messages across calls;
 * CENTER, TOP and BOTTOM reset/override that placement. This routine
 * does not release its temporary Box image, an existing ownership gap.
 */
VOID egDisplayMessage(IN CHAR16 *Text, EG_PIXEL *BGColor, UINTN PositionCode) {
    UINTN BoxWidth, BoxHeight;
    static UINTN Position = 1;
    EG_IMAGE *Box;

    if ((Text != NULL) && (BGColor != NULL)) {
        egMeasureText(Text, &BoxWidth, &BoxHeight);
        BoxWidth += 14;
        BoxHeight *= 2;
        if (BoxWidth > egScreenWidth)
            BoxWidth = egScreenWidth;
        Box = egCreateFilledImage(BoxWidth, BoxHeight, FALSE, BGColor);
        egRenderText(Text, Box, 7, BoxHeight / 4, (BGColor->r + BGColor->g + BGColor->b) / 3);
        switch (PositionCode) {
             case CENTER:
                 Position = (egScreenHeight - BoxHeight) / 2;
                 break;
             case BOTTOM:
                 Position = egScreenHeight - (BoxHeight * 2);
                 break;
             case TOP:
                 Position = 1;
                 break;
             default:  
                 Position += BoxHeight + (BoxHeight / 10);
                 break;
        }  
        egDrawImage(Box, (egScreenWidth - BoxWidth) / 2, Position);
        if ((PositionCode == CENTER) || (Position >= egScreenHeight - (BoxHeight * 5)))
            Position = 1;
    }  
}  

EG_IMAGE * egCopyScreen(VOID) {
    return egCopyScreenArea(0, 0, egScreenWidth, egScreenHeight);
}  

/**
 * Allocate an owned image and request a video-to-buffer transfer from the active
 * graphics protocol. The firmware transfer status is ignored, so a non-NULL
 * return establishes allocation but cannot establish that the framebuffer was
 * successfully read.
 */
EG_IMAGE * egCopyScreenArea(UINTN XPos, UINTN YPos, UINTN Width, UINTN Height) {
    EG_IMAGE *Image = NULL;

    if (!egHasGraphics)
        return NULL;

    Image = egCreateImage(Width, Height, FALSE);
    if (Image == NULL) {
        return NULL;
    }

    if (GraphicsOutput != NULL) {
        refit_call10_wrapper(GraphicsOutput->Blt, GraphicsOutput,
                             (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)Image->PixelData,
                             EfiBltVideoToBltBuffer, XPos, YPos, 0, 0,
                             Image->Width, Image->Height, 0);
    } else if (UgaDraw != NULL) {
        refit_call10_wrapper(UgaDraw->Blt, UgaDraw, (EFI_UGA_PIXEL *)Image->PixelData,
                             EfiUgaVideoToBltBuffer, XPos, YPos, 0, 0, Image->Width,
                             Image->Height, 0);
    }
    return Image;
}  

/*
 * Copy the framebuffer, encode BMP and persist it on the selected ESP.
 * The output image and encoded bytes have separate ownership. If egFindESP
 * fails, the current early return occurs after encoding without freeing
 * FileData; that is a potential pool leak, not an intentionally retained
 * screenshot buffer. Capture is a UI diagnostic, not loader validation.
 */
VOID egScreenShot(VOID)
{
    EFI_STATUS      Status;
    EG_IMAGE        *Image;
    UINT8           *FileData;
    UINTN           FileDataLength;
    UINTN           Index;
    UINTN           ssNum;
    CHAR16          Filename[80];
    EFI_FILE*       BaseDir;

    Image = egCopyScreen();
    if (Image == NULL) {
       Print(L"Error: Unable to take screen shot\n");
       LOG(1, LOG_LINE_NORMAL, L"Error: Unable to take screen shot (Image is NULL)");
       goto bailout_wait;
    }

    egEncodeBMP(Image, &FileData, &FileDataLength);
    egFreeImage(Image);
    if (FileData == NULL) {
        Print(L"Error egEncodeBMP returned NULL\n");
        LOG(1, LOG_LINE_NORMAL, L"Error: egEncodeBMP returned NULL");
        goto bailout_wait;
    }

    Status = egFindESP(&BaseDir);
    if (EFI_ERROR(Status))
        return;

    ssNum = 001;
    do {
       SPrint(Filename, 80, L"screenshot_%03d.bmp", ssNum++);
    } while (FileExists(BaseDir, Filename));

    Status = egSaveFile(BaseDir, Filename, FileData, FileDataLength);
    MyFreePool(FileData);
    if (CheckError(Status, L"in egSaveFile()")) {
        goto bailout_wait;
    }

    return;

bailout_wait:
    egSetGraphicsModeEnabled(FALSE);
    refit_call3_wrapper(BS->WaitForEvent, 1, &ST->ConIn->WaitForKey, &Index);
}
