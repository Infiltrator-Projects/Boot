/* Firmware console, GOP and display-state interfaces.
 * Graphical resources are invalidated by relevant display-mode changes.
 * Text-mode fallback remains available if rendering fails.
 */

#ifndef __SCREEN_H_
#define __SCREEN_H_

#ifdef __MAKEWITH_GNUEFI
#include "efi.h"
#include "efilib.h"
#else
#include "../include/tiano_includes.h"
#endif

#include "libeg.h"

/*
 * This sentinel leaves firmware's current text mode alone. A numeric mode or
 * a resolution request uses a different path during screen setup.
 */
#define DONT_CHANGE_TEXT_MODE 1024  

#define ATTR_BASIC (EFI_LIGHTGRAY | EFI_BACKGROUND_BLACK)
#define ATTR_ERROR (EFI_YELLOW | EFI_BACKGROUND_BLACK)
#define ATTR_BANNER (EFI_WHITE | EFI_BACKGROUND_BLUE)
#define ATTR_CHOICE_BASIC ATTR_BASIC
#define ATTR_CHOICE_CURRENT (EFI_WHITE | EFI_BACKGROUND_GREEN)
#define ATTR_SCROLLARROW (EFI_LIGHTGREEN | EFI_BACKGROUND_BLACK)

#define LAYOUT_BANNER_YGAP 32

#define CENTER 0
#define BOTTOM 1
#define TOP 2
#define NEXTLINE 3

extern UINTN ConWidth;
extern UINTN ConHeight;
extern CHAR16 *BlankLine;

extern UINTN UGAWidth;
extern UINTN UGAHeight;
extern BOOLEAN AllowGraphicsMode;

extern EG_PIXEL StdBackgroundPixel;
extern EG_PIXEL MenuBackgroundPixel;

/*
 * Screen initialization queries firmware console/GOP and allocates drawing
 * state. BeginExternalScreen and FinishExternalScreen bracket EFI child
 * image execution; the graphics mode and cached dimensions may differ
 * afterward.
 */
/*
 * Temporary startup instrumentation. Change this one constant to 0 when
 * diagnostics are no longer required; all call sites then become no-ops.
 * EFI driver load success is deliberately not labelled a successful mount.
 */
#define BOOT_STARTUP_PROGRESS_ENABLED 1
#define BOOT_PROGRESS_ACTIVE 0
#define BOOT_PROGRESS_OK     1
#define BOOT_PROGRESS_ERROR  2
#define BOOT_PROGRESS_INFO   3

#if BOOT_STARTUP_PROGRESS_ENABLED
VOID BootStartupProgressBegin(VOID);
VOID BootStartupProgressReport(IN CHAR16 *Message, IN UINTN Result);
VOID BootStartupProgressEnd(VOID);
#else
#define BootStartupProgressBegin()                 ((VOID)0)
#define BootStartupProgressReport(Message, Result) ((VOID)0)
#define BootStartupProgressEnd()                   ((VOID)0)
#endif

VOID InitScreen(VOID);
VOID SetupScreen(VOID);
VOID BeginTextScreen(IN CHAR16 *Title);
VOID BeginExternalScreen(IN BOOLEAN UseGraphicsMode, IN CHAR16 *Title);
VOID FinishExternalScreen(VOID);
VOID TerminateScreen(VOID);
VOID DrawScreenHeader(IN CHAR16 *Title);

/*
 * Input and pause helpers operate on ConIn/Boot Services events;
 * PauseSeconds passes microseconds to Stall, while error prompts consume
 * fresh input rather than stale keystrokes.
 */
VOID EndlessIdleLoop(VOID);
BOOLEAN ReadAllKeyStrokes(VOID);
VOID PrintUglyText(IN CHAR16 *Text, UINTN PositionCode);
VOID PauseForKey(VOID);
VOID PauseSeconds(UINTN Seconds);

BOOLEAN CheckFatalError(IN EFI_STATUS Status, IN CHAR16 *where);
BOOLEAN CheckError(IN EFI_STATUS Status, IN CHAR16 *where);

/*
 * Text/graphics switches mutate firmware presentation state, not menu
 * entries or filesystem selection. The image blit inputs are borrowed
 * graphics surfaces; blit status is not exposed through these VOID APIs.
 */
VOID SwitchToText(IN BOOLEAN CursorEnabled);
VOID SwitchToGraphics(VOID);

VOID SwitchToGraphicsAndClear(VOID);
VOID BltClearScreen(IN BOOLEAN ShowBanner);
VOID BltImage(IN EG_IMAGE *Image, IN UINTN XPos, IN UINTN YPos);
VOID BltImageAlpha(IN EG_IMAGE *Image, IN UINTN XPos, IN UINTN YPos, IN EG_PIXEL *BackgroundPixel);

VOID BltImageCompositeBadge(IN EG_IMAGE *BaseImage, IN EG_IMAGE *TopImage, IN EG_IMAGE *BadgeImage, IN UINTN XPos, IN UINTN YPos);

#endif
