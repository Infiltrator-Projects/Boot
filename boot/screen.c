/*
 * Firmware graphics/text mode, output device and screen lifetime management.
 * UEFI GOP/console state must be restored or deliberately transitioned across
 * child-image execution. A renderer failure must not be treated as disk failure.
 * Screen capture, compositing scratch images and cached console rows have
 * independent allocation lifetimes. Changing text/graphics mode can
 * invalidate layout dimensions without affecting loader discovery state.
 */

#include "global.h"
#include "screen.h"
#include "theme.h"
#include "config.h"
#include "libegint.h"
#include "lib.h"
#include "menu.h"
#include "mystrings.h"
#include "log.h"
#include "../include/refit_call_wrapper.h"

#include "../include/egemb_boot_banner.h"

UINTN ConWidth;
UINTN ConHeight;
CHAR16 *BlankLine = NULL;

UINTN UGAWidth;
UINTN UGAHeight;
BOOLEAN AllowGraphicsMode;
BOOLEAN HaveResized = FALSE;

EG_PIXEL StdBackgroundPixel  = { 0xbf, 0xbf, 0xbf, 0 };
EG_PIXEL MenuBackgroundPixel = { 0xbf, 0xbf, 0xbf, 0 };
EG_PIXEL DarkBackgroundPixel = { 0x0, 0x0, 0x0, 0 };

static BOOLEAN GraphicsScreenDirty;

/*
 * The blank row is a cache sized in CHAR16 cells from a firmware-reported
 * console width. Reallocation invalidates the previous row; a failed or
 * oversized allocation leaves NULL and the header must skip row erasure.
 */
static VOID PrepareBlankLine(VOID) {
    UINTN i;

    MyFreePool(BlankLine);
    BlankLine = NULL;

    if (ConWidth >= ((UINTN)-1) / sizeof(CHAR16))
        return;

    BlankLine = AllocatePool((ConWidth + 1) * sizeof(CHAR16));
    if (BlankLine == NULL)
        return;
    for (i = 0; i < ConWidth; i++)
        BlankLine[i] = L' ';
    BlankLine[i] = L'\0';
}

/*
 * Captures initial firmware display capabilities before switching modes.
 * Later graphics operations depend on usable console/graphics protocols.
 */
VOID InitScreen(VOID)
{
    LOG(1, LOG_LINE_NORMAL, L"Entering InitScreen()");

    egInitScreen();

    if (egHasGraphicsMode()) {
        LOG(2, LOG_LINE_NORMAL, L"Have graphics mode; setting screen size");
        egGetScreenSize(&UGAWidth, &UGAHeight);
        AllowGraphicsMode = TRUE;
    } else {
        LOG(2, LOG_LINE_NORMAL, L"No graphics mode detected; setting text mode");
        AllowGraphicsMode = FALSE;
        egSetTextMode(GlobalConfig.RequestedTextMode);
        egSetGraphicsModeEnabled(FALSE);    
    }
    GraphicsScreenDirty = TRUE;

    refit_call2_wrapper(ST->ConOut->EnableCursor, ST->ConOut, FALSE);

    if (refit_call4_wrapper(ST->ConOut->QueryMode, ST->ConOut,
                            ST->ConOut->Mode->Mode, &ConWidth,
                            &ConHeight) != EFI_SUCCESS) {

        ConWidth = 80;
        ConHeight = 25;
    }

    PrepareBlankLine();

    if (GlobalConfig.TextOnly && (GlobalConfig.ScreensaverTime != -1))
       DrawScreenHeader(L"Initializing...");
}

/**
 * A requested width with zero height denotes a firmware mode number. Resolve it
 * to dimensions before changing graphics mode. A text mode change can also alter
 * the pixel geometry, so query the firmware again before sizing the menu.
 * HaveResized prevents a second HiDPI doubling on a rescan.
 */
VOID SetupScreen(VOID)
{
    UINTN NewWidth, NewHeight;

    LOG(1, LOG_LINE_NORMAL, L"Setting screen resolution and mode");

    if ((GlobalConfig.RequestedScreenWidth > 0) && (GlobalConfig.RequestedScreenHeight == 0))
       egGetResFromMode(&(GlobalConfig.RequestedScreenWidth), &(GlobalConfig.RequestedScreenHeight));

    if ((GlobalConfig.RequestedScreenWidth > 0) && (GlobalConfig.RequestedScreenHeight > 0)) {
       UGAWidth = (UGAWidth < GlobalConfig.RequestedScreenWidth) ? UGAWidth : GlobalConfig.RequestedScreenWidth;
       UGAHeight = (UGAHeight < GlobalConfig.RequestedScreenHeight) ? UGAHeight : GlobalConfig.RequestedScreenHeight;
       LOG(2, LOG_LINE_NORMAL, L"Recording current resolution as %dx%d", UGAWidth, UGAHeight);
    }

    if (egSetTextMode(GlobalConfig.RequestedTextMode)) {
       egGetScreenSize(&NewWidth, &NewHeight);
       if ((NewWidth > UGAWidth) || (NewHeight > UGAHeight)) {
          UGAWidth = NewWidth;
          UGAHeight = NewHeight;
          LOG(2, LOG_LINE_NORMAL, L"After setting text mode, recording new current resolution as %dx%d",
              UGAWidth, UGAHeight);
       }
       if ((UGAWidth > GlobalConfig.RequestedScreenWidth) ||
           (UGAHeight > GlobalConfig.RequestedScreenHeight)) {
           LOG(2, LOG_LINE_NORMAL, L"Adjusting requested screen size based on actual screen size");

           GlobalConfig.RequestedScreenWidth = UGAWidth;
           GlobalConfig.RequestedScreenHeight = UGAHeight;
       }  
    }

    if (GlobalConfig.RequestedScreenWidth > 0) {
       LOG(2, LOG_LINE_NORMAL, L"Setting screen size to %dx%d", GlobalConfig.RequestedScreenWidth,
           GlobalConfig.RequestedScreenHeight);
       egSetScreenSize(&(GlobalConfig.RequestedScreenWidth), &(GlobalConfig.RequestedScreenHeight));
       egGetScreenSize(&UGAWidth, &UGAHeight);
    }  

    if (GlobalConfig.TextOnly) {
        LOG(2, LOG_LINE_NORMAL, L"Setting text-only mode");

        AllowGraphicsMode = FALSE;
        SwitchToText(FALSE);
    } else if (AllowGraphicsMode) {

        if ((UGAWidth >= HIDPI_MIN) && !HaveResized) {
            LOG(2, LOG_LINE_NORMAL, L"Doubling icon sizes for HiDPI display");
            GlobalConfig.IconSizes[ICON_SIZE_BADGE] *= 2;
            GlobalConfig.IconSizes[ICON_SIZE_SMALL] *= 2;
            GlobalConfig.IconSizes[ICON_SIZE_BIG] *= 2;
            GlobalConfig.IconSizes[ICON_SIZE_MOUSE] *= 2;
            HaveResized = TRUE;
        }  
        SwitchToGraphics();
        if (GlobalConfig.ScreensaverTime != -1) {
           BltClearScreen(TRUE);
        } else {  
           GraphicsScreenDirty = TRUE;
        }
    }
}  

/**
 * Switch firmware output to text and apply cursor policy. Requery the
 * console dimensions, using 80x25 on failure before allocating the
 * optional row-clearing buffer.
 */
VOID SwitchToText(IN BOOLEAN CursorEnabled)
{
    egSetGraphicsModeEnabled(FALSE);
    refit_call2_wrapper(ST->ConOut->EnableCursor, ST->ConOut, CursorEnabled);

    if (refit_call4_wrapper(ST->ConOut->QueryMode, ST->ConOut, ST->ConOut->Mode->Mode, &ConWidth, &ConHeight) != EFI_SUCCESS) {

        ConWidth = 80;
        ConHeight = 25;
    }
    PrepareBlankLine();
}

/**
 * Enable graphics output and mark the previous image invalid.
 * Changing mode does not paint the new menu; callers must perform
 * the appropriate clear or incremental redraw afterwards.
 */
VOID SwitchToGraphics(VOID)
{
    if (AllowGraphicsMode && !egIsGraphicsModeEnabled()) {
        egSetGraphicsModeEnabled(TRUE);
        GraphicsScreenDirty = TRUE;
    }
}

/**
 * Prepare a firmware text header and disable the console cursor.
 * The title pointer is borrowed; no keyboard events are consumed here.
 */
VOID BeginTextScreen(IN CHAR16 *Title)
{
    DrawScreenHeader(Title);
    SwitchToText(FALSE);
}

/*
 * The child can request graphics, but AllowGraphicsMode decides whether that
 * request can be honoured. The text path draws the title and enables the
 * firmware console cursor before control leaves the menu.
 */
VOID BeginExternalScreen(IN BOOLEAN UseGraphicsMode, IN CHAR16 *Title)
{
    if (!AllowGraphicsMode)
        UseGraphicsMode = FALSE;

    if (UseGraphicsMode) {
        SwitchToGraphics();
        BltClearScreen(FALSE);
    } else {
        egClearScreen(&DarkBackgroundPixel);
        DrawScreenHeader(Title);
        SwitchToText(TRUE);
    }
}

/*
 * A returning image may have drawn into the framebuffer or switched modes.
 * Mark the background dirty before SetupScreen reselects BOOT's presentation
 * state.
 */
VOID FinishExternalScreen(VOID)
{

    GraphicsScreenDirty = TRUE;

    SetupScreen();
}

/*
 * Restore basic console attributes and visible cursor state for
 * handoff. This is console cleanup, not an EFI image exit or
 * reconstruction of the prior graphical framebuffer.
 */
VOID TerminateScreen(VOID)
{

    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
    refit_call1_wrapper(ST->ConOut->ClearScreen, ST->ConOut);

    refit_call2_wrapper(ST->ConOut->EnableCursor, ST->ConOut, TRUE);
}

/*
 * Reset graphics and text layers before rendering the title banner.
 * Firmware console coordinates and pixel coordinates are separate
 * spaces, so a successful text clear does not imply graphics state
 * was restored by a preceding EFI application.
 */
VOID DrawScreenHeader(IN CHAR16 *Title)
{
    UINTN y;

    egClearScreen(&DarkBackgroundPixel);  
    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
    refit_call1_wrapper(ST->ConOut->ClearScreen, ST->ConOut);  

    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BANNER);
    if (BlankLine != NULL) {
        for (y = 0; y < 3; y++) {
            refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 0, y);
            Print(BlankLine);
        }
    }

    refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 3, 1);
    Print(L"BOOT - %s", Title);

    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
    refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 0, 4);
}

/**
 * Drain all queued EFI keystrokes. TRUE means one or more events were
 * consumed, not that any constituted a valid selection. This operation
 * deliberately mutates the firmware input queue.
 */
BOOLEAN ReadAllKeyStrokes(VOID)
{
    BOOLEAN       GotKeyStrokes;
    EFI_STATUS    Status;
    EFI_INPUT_KEY key;

    GotKeyStrokes = FALSE;
    for (;;) {
        Status = refit_call2_wrapper(ST->ConIn->ReadKeyStroke, ST->ConIn, &key);
        if (Status == EFI_SUCCESS) {
            GotKeyStrokes = TRUE;
            continue;
        }
        break;
    }
    return GotKeyStrokes;
}

/* Emergency/status text selects Apple graphics overlay only when graphics
 * output is available; other platforms use ConOut. The incoming string is
 * borrowed, and this path is intentionally independent of menu selection.
 * Marking GraphicsScreenDirty requests a subsequent presentation refresh;
 * it does not establish that firmware displayed the overlay successfully.
 */
VOID PrintUglyText(IN CHAR16 *Text, IN UINTN PositionCode) {
    EG_PIXEL BGColor = COLOR_RED;

    if (Text) {
        if (AllowGraphicsMode && MyStriCmp(L"Apple", ST->FirmwareVendor) && egIsGraphicsModeEnabled()) {
            egDisplayMessage(Text, &BGColor, PositionCode);
            GraphicsScreenDirty = TRUE;
        } else {  
            Print(Text);
            Print(L"\n");
        }  
    }  
}  

/**
 * Prevent a stale queued key from acknowledging an error prompt. Drain
 * input first; if keys were pending, pause and drain again before waiting
 * without a timeout for a fresh input event.
 */
VOID PauseForKey(VOID)
{
    UINTN index;

    Print(L"\n");
    PrintUglyText(L"* Hit any key to continue *", BOTTOM);

    if (ReadAllKeyStrokes()) {   
        refit_call1_wrapper(BS->Stall, 5000000);      
        ReadAllKeyStrokes();     
    }

    refit_call3_wrapper(BS->WaitForEvent, 1, &ST->ConIn->WaitForKey, &index);
    ReadAllKeyStrokes();         
}

/*
 * Convert seconds to the microsecond unit expected by Boot Services
 * Stall. The multiplication is performed in UINTN and is not
 * guarded against overflow for extreme caller-supplied values.
 */
VOID PauseSeconds(UINTN Seconds) {
     refit_call1_wrapper(BS->Stall, 1000000 * Seconds);
}  

/**
 * Enter an intentionally non-returning EFI event loop, clearing queued
 * keyboard input before each wait. There is no timeout or loader handoff;
 * this is a terminal diagnostic state rather than a recovery mechanism.
 */
VOID EndlessIdleLoop(VOID)
{
    UINTN index;

    for (;;) {
        ReadAllKeyStrokes();
        refit_call3_wrapper(BS->WaitForEvent, 1, &ST->ConIn->WaitForKey, &index);
    }
}

/*
 * Return FALSE for firmware success or warning statuses. For an EFI error,
 * display and log the status with its operation context and return TRUE. The
 * caller decides how to terminate or unwind.
 */
BOOLEAN CheckFatalError(IN EFI_STATUS Status, IN CHAR16 *where)
{
    CHAR16 *Temp = NULL;

    if (!EFI_ERROR(Status))
        return FALSE;

#ifdef __MAKEWITH_GNUEFI
    CHAR16 ErrorName[64];
    StatusToString(ErrorName, Status);
    Temp = PoolPrint(L"Fatal Error: %s %s", ErrorName, where);
#else
    Temp = PoolPrint(L"Fatal Error: %s %s", Status, where);
#endif
    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_ERROR);
    PrintUglyText(Temp, NEXTLINE);
    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
    LOG(1, LOG_LINE_NORMAL, Temp);
    MyFreePool(Temp);

    return TRUE;
}  

/**
 * Convert only EFI error-class statuses to an interactive diagnostic.
 * A warning returns FALSE. On error, log and display a temporary formatted
 * message, wait for acknowledgement, then free that message allocation.
 */
BOOLEAN CheckError(IN EFI_STATUS Status, IN CHAR16 *where)
{
    CHAR16 *Temp = NULL;

    if (!EFI_ERROR(Status))
        return FALSE;

#ifdef __MAKEWITH_GNUEFI
    CHAR16 ErrorName[64];
    StatusToString(ErrorName, Status);
    Temp = PoolPrint(L"Error: %s %s", ErrorName, where);
#else
    Temp = PoolPrint(L"Error: %r %s", Status, where);
#endif
    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_ERROR);
    PrintUglyText(Temp, NEXTLINE);
    refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
    LOG(1, LOG_LINE_NORMAL, Temp);
    PauseForKey();
    MyFreePool(Temp);

    return TRUE;
}  

/**
 * Return to graphics and repaint only if GraphicsScreenDirty indicates
 * a stale screen. Reusing an already-clean buffer avoids discarding
 * the currently visible menu composition.
 */
VOID SwitchToGraphicsAndClear(VOID)
{
    SwitchToGraphics();
    if (GraphicsScreenDirty)
        BltClearScreen(TRUE);
}

/**
 * The banner persists between draws while ScreenBackground is recreated.
 * A resized banner transfers ownership to its replacement. A failed
 * screen-copy allocation may leave ScreenBackground unavailable.
 */
VOID BltClearScreen(BOOLEAN ShowBanner)
{
    static EG_IMAGE *Banner = NULL;
    EG_IMAGE *NewBanner = NULL;
    INTN BannerPosX, BannerPosY;
    EG_PIXEL Black = { 0x0, 0x0, 0x0, 0 };


    /*
     * A bundled, pre-rasterised luxury background is the default presentation.
     * User-requested custom banners retain their old behaviour; if the image
     * is missing, BOOT falls back to the existing banner rather than suppress
     * boot controls. Background and live UI never share a baked-in OS identity.
     */
    if (ShowBanner && !GlobalConfig.BannerFileName &&
        GlobalConfig.ScreensaverTime != -1) {
        EG_IMAGE *ThemeBackground=BootThemeBackground(UGAWidth,UGAHeight);
        if (ThemeBackground) {
            MenuBackgroundPixel.b=8;
            MenuBackgroundPixel.g=11;
            MenuBackgroundPixel.r=5;
            MenuBackgroundPixel.a=0;
            egDrawImage(ThemeBackground,0,0);
            GlobalConfig.BannerBottomEdge=0;
            if (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_BANNER))
                GlobalConfig.BannerBottomEdge=BootThemeDrawWordmark(ThemeBackground);
            GraphicsScreenDirty=FALSE;
            egFreeImage(GlobalConfig.ScreenBackground);
            GlobalConfig.ScreenBackground=egCopyScreen();
            return;
        }
    }
    if (ShowBanner && !(GlobalConfig.HideUIFlags & HIDEUI_FLAG_BANNER)) {

        if (Banner == NULL) {
            if (GlobalConfig.BannerFileName)
                Banner = egLoadImage(SelfDir, GlobalConfig.BannerFileName, FALSE);
            if (Banner == NULL)
                Banner = egPrepareEmbeddedImage(&egemb_boot_banner, FALSE);
        }

        if (Banner) {
           if (GlobalConfig.BannerScale == BANNER_FILLSCREEN) {
              if ((Banner->Height != UGAHeight) || (Banner->Width != UGAWidth)) {
                 NewBanner = egScaleImage(Banner, UGAWidth, UGAHeight);
              }  
           } else if ((Banner->Width > UGAWidth) || (Banner->Height > UGAHeight)) {
              NewBanner = egCropImage(Banner, 0, 0, (Banner->Width > UGAWidth) ? UGAWidth : Banner->Width,
                                      (Banner->Height > UGAHeight) ? UGAHeight : Banner->Height);
           }  
           if (NewBanner) {
              egFreeImage(Banner);
              Banner = NewBanner;
           }
           MenuBackgroundPixel = Banner->PixelData[0];
        }  

        if (GlobalConfig.ScreensaverTime != -1)
           egClearScreen(&MenuBackgroundPixel);
        else
           egClearScreen(&Black);

        if (Banner != NULL) {
            BannerPosX = (Banner->Width < UGAWidth) ? ((UGAWidth - Banner->Width) / 2) : 0;
            BannerPosY = (INTN) (ComputeRow0PosY() / 2) - (INTN) Banner->Height;
            if (BannerPosY < 0)
               BannerPosY = 0;
            GlobalConfig.BannerBottomEdge = BannerPosY + Banner->Height;
            if (GlobalConfig.ScreensaverTime != -1)
               BltImage(Banner, (UINTN) BannerPosX, (UINTN) BannerPosY);
        }

    } else {  

        egClearScreen(&MenuBackgroundPixel);
    }

    GraphicsScreenDirty = FALSE;
    egFreeImage(GlobalConfig.ScreenBackground);
    GlobalConfig.ScreenBackground = egCopyScreen();
}  

/**
 * Blit a borrowed image and mark graphics as dirty. The function
 * neither owns nor releases the caller's image pixels.
 */
VOID BltImage(IN EG_IMAGE *Image, IN UINTN XPos, IN UINTN YPos)
{
    egDrawImage(Image, XPos, YPos);
    GraphicsScreenDirty = TRUE;
}

/**
 * Composite borrowed source pixels into an owned temporary buffer.
 * If allocation fails, the framebuffer and its dirty flag stay unchanged;
 * the temporary image must never be passed as NULL to egComposeImage.
 */
VOID BltImageAlpha(IN EG_IMAGE *Image, IN UINTN XPos, IN UINTN YPos, IN EG_PIXEL *BackgroundPixel)
{
    EG_IMAGE *CompImage;

    CompImage = egCreateFilledImage(Image->Width, Image->Height, FALSE, BackgroundPixel);
    if (CompImage == NULL)
        return;
    egComposeImage(CompImage, Image, 0, 0);

    egDrawImage(CompImage, XPos, YPos);
    egFreeImage(CompImage);
    GraphicsScreenDirty = TRUE;
}

/**
 * Centre the top icon in a private copy of the base tile. Position the badge
 * relative to the clipped top icon and keep an eight-pixel inset. Only the
 * composed copy is freed after drawing, allowing the same selection, icon and
 * badge assets to be reused on the next repaint.
 */
VOID BltImageCompositeBadge(IN EG_IMAGE *BaseImage,
                            IN EG_IMAGE *TopImage,
                            IN EG_IMAGE *BadgeImage,
                            IN UINTN XPos,
                            IN UINTN YPos)
{
     UINTN TotalWidth = 0, TotalHeight = 0, CompWidth = 0, CompHeight = 0, OffsetX = 0, OffsetY = 0;
     EG_IMAGE *CompImage = NULL;

     if (BaseImage != NULL) {
         CompImage = egCopyImage(BaseImage);
         TotalWidth  = BaseImage->Width;
         TotalHeight = BaseImage->Height;
     }

     if ((TopImage != NULL) && (CompImage != NULL)) {
         CompWidth = TopImage->Width;
         if (CompWidth > TotalWidth)
               CompWidth = TotalWidth;
         OffsetX = (TotalWidth - CompWidth) >> 1;
         CompHeight = TopImage->Height;
         if (CompHeight > TotalHeight)
               CompHeight = TotalHeight;
         OffsetY = (TotalHeight - CompHeight) >> 1;
         egComposeImage(CompImage, TopImage, OffsetX, OffsetY);
     }

     if (BadgeImage != NULL && CompImage != NULL && (BadgeImage->Width + 8) < CompWidth && (BadgeImage->Height + 8) < CompHeight) {
         OffsetX += CompWidth  - 8 - BadgeImage->Width;
         OffsetY += CompHeight - 8 - BadgeImage->Height;
         egComposeImage(CompImage, BadgeImage, OffsetX, OffsetY);
     }

     if (CompImage != NULL) {
         if (CompImage->HasAlpha)
             egDrawImageWithTransparency(CompImage, NULL, XPos, YPos, CompImage->Width, CompImage->Height);
         else
             egDrawImage(CompImage, XPos, YPos);
         egFreeImage(CompImage);
         GraphicsScreenDirty = TRUE;
     }
}


#if BOOT_STARTUP_PROGRESS_ENABLED
/*
 * Temporary diagnostic splash, independent of the menu wallpaper and the
 * filesystem driver path. Graphics must be initialised once by efi_main
 * before Begin, while BOOT's own SelfDir is still available for the installed
 * font collection. All history lives in fixed-size records; a failed display
 * allocation does not alter boot discovery or handoff.
 *
 * This is an activity indicator, not an elapsed-time or percent-complete
 * estimate. It deliberately avoids repainting on a timer during synchronous
 * firmware calls: the last visible activity identifies a slow stage.
 */
#define BOOT_PROGRESS_ROWS 10
#define BOOT_PROGRESS_CHARS 96

static BOOLEAN BootProgressVisible;
static UINTN BootProgressCount;
static UINTN BootProgressTick;
static CHAR16 BootProgressLines[BOOT_PROGRESS_ROWS][BOOT_PROGRESS_CHARS];
static UINTN BootProgressResults[BOOT_PROGRESS_ROWS];

/* Repaint a compact console-shaped panel using the already active GOP/UGA. */
static VOID BootStartupProgressPaint(VOID)
{
    UINTN screenWidth = 0, screenHeight = 0;
    UINTN width, height, index, limit, rowHeight;
    UINTN trackWidth, segmentWidth, offset;
    EG_IMAGE *panel;
    EG_PIXEL background = { 12, 10, 8, 0 };
    EG_PIXEL surface = { 25, 23, 19, 0 };
    EG_PIXEL track = { 46, 43, 37, 0 };
    EG_PIXEL cyan = { 233, 220, 39, 0 };
    EG_PIXEL green = { 135, 193, 94, 0 };
    EG_PIXEL red = { 93, 93, 227, 0 };
    EG_PIXEL muted = { 120, 115, 109, 0 };
    EG_PIXEL *indicator;

    /*
     * GOP/UGA drawing does not require the optional ConsoleControl protocol.
     * A protocol-less firmware reports FALSE from egIsGraphicsModeEnabled()
     * even though the ordinary graphical menu works. Use the actual graphics
     * availability, and request graphics mode where firmware supports it.
     */
    if (!BootProgressVisible || !AllowGraphicsMode || !egHasGraphicsMode())
        return;
    SwitchToGraphics();
    egGetScreenSize(&screenWidth, &screenHeight);
    if (screenWidth < 460 || screenHeight < 300)
        return;

    width = screenWidth > 1000 ? 940 : screenWidth - 40;
    height = screenHeight > 510 ? 466 : screenHeight - 32;
    trackWidth = width - 64;
    segmentWidth = trackWidth / 6;
    if (!segmentWidth)
        return;

    panel = egCreateFilledImage(width, height, FALSE, &surface);
    if (panel == NULL)
        return;

    egFillImageArea(panel, 26, 24, width - 52, 2, &cyan);
    egRenderTextRole(L"BOOT  /  STARTUP DIAGNOSTICS", panel,
                     32, 35, 0, MB_FONT_BOLD);
    egRenderTextRole(L"Temporary display - EFI driver acceptance is not a filesystem mount",
                     panel, 32, 68, 0, MB_FONT_BODY);
    egFillImageArea(panel, 32, 108, trackWidth, 8, &track);
    offset = ((BootProgressTick - 1) * 79) % (trackWidth - segmentWidth + 1);
    egFillImageArea(panel, 32 + offset, 108, segmentWidth, 8, &cyan);

    rowHeight = egGetFontHeight() + 7;
    if (rowHeight < 24)
        rowHeight = 24;
    limit = (height - 142) / rowHeight;
    if (limit > BootProgressCount)
        limit = BootProgressCount;

    for (index = 0; index < limit; ++index) {
        UINTN sourceIndex = BootProgressCount - limit + index;
        UINTN y = 134 + index * rowHeight;
        indicator = &muted;
        if (BootProgressResults[sourceIndex] == BOOT_PROGRESS_ACTIVE)
            indicator = &cyan;
        else if (BootProgressResults[sourceIndex] == BOOT_PROGRESS_OK)
            indicator = &green;
        else if (BootProgressResults[sourceIndex] == BOOT_PROGRESS_ERROR)
            indicator = &red;

        egFillImageArea(panel, 33, y + 5, 7, 7, indicator);
        egRenderTextRole(BootProgressLines[sourceIndex], panel, 49, y,
                         0, MB_FONT_BODY);
    }

    egClearScreen(&background);
    egDrawImage(panel, (screenWidth - width) / 2,
                (screenHeight - height) / 2);
    egFreeImage(panel);
}

/*
 * Draw an immediate background before any TTF I/O. If font preparation is
 * slow, the firmware has already switched into graphics mode and displayed
 * the diagnostic background rather than appearing to hang at a blank screen.
 */
VOID BootStartupProgressBegin(VOID)
{
    EG_PIXEL background = { 12, 10, 8, 0 };
    EG_PIXEL surface = { 25, 23, 19, 0 };
    EG_PIXEL cyan = { 233, 220, 39, 0 };
    EG_IMAGE *splash;
    UINTN width = 0, height = 0;

    BootProgressCount = 0;
    BootProgressTick = 0;
    BootProgressVisible = FALSE;
    if (!AllowGraphicsMode || !egHasGraphicsMode())
        return;
    /*
     * ConsoleControl is optional. A valid GOP/UGA target is sufficient to
     * blit our diagnostic surface even when no console mode query exists.
     */
    SwitchToGraphics();
    BootProgressVisible = TRUE;
    egClearScreen(&background);

    /*
     * Paint a recognisable cyan status stripe before the first font read.
     * The following report will replace it with labelled progress once
     * the renderer has parsed the installed font files.
     */
    egGetScreenSize(&width, &height);
    if (width >= 460 && height >= 300) {
        splash = egCreateFilledImage(360, 62, FALSE, &surface);
        if (splash != NULL) {
            egFillImageArea(splash, 12, 12, 336, 5, &cyan);
            egFillImageArea(splash, 12, 29, 116, 8, &cyan);
            egDrawImage(splash, (width - 360) / 2, (height - 62) / 2);
            egFreeImage(splash);
        }
    }
    BootStartupProgressReport(L"Graphical diagnostic display initialised",
                              BOOT_PROGRESS_OK);
}

/* Copy the caller's transient message before it releases any allocations. */
VOID BootStartupProgressReport(IN CHAR16 *Message, IN UINTN Result)
{
    UINTN i, row;
    if (!BootProgressVisible || Message == NULL)
        return;

    if (BootProgressCount == BOOT_PROGRESS_ROWS) {
        for (row = 1; row < BOOT_PROGRESS_ROWS; ++row) {
            for (i = 0; i < BOOT_PROGRESS_CHARS; ++i)
                BootProgressLines[row - 1][i] = BootProgressLines[row][i];
            BootProgressResults[row - 1] = BootProgressResults[row];
        }
        BootProgressCount--;
    }

    row = BootProgressCount++;
    for (i = 0; i + 1 < BOOT_PROGRESS_CHARS && Message[i] != 0; ++i)
        BootProgressLines[row][i] = Message[i];
    BootProgressLines[row][i] = 0;
    BootProgressResults[row] = Result;
    ++BootProgressTick;
    BootStartupProgressPaint();
}

/* The regular menu repaint completely replaces this temporary surface. */
VOID BootStartupProgressEnd(VOID)
{
    BootProgressVisible = FALSE;
}
#endif /* BOOT_STARTUP_PROGRESS_ENABLED */
