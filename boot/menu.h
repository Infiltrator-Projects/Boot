/*
 * BOOT / Menu selection API
 *
 * Menu layout, scroll and selection-result interface.
 *
 * Entry pointers are borrowed; selection results describe UI transitions.
 * Ownership and selected-entry validity follow the active scan.
 */

#ifndef __BOOT_MENU_H_
#define __BOOT_MENU_H_

#ifdef __MAKEWITH_GNUEFI
#include "efi.h"
#include "efilib.h"
#else
#include "../include/tiano_includes.h"
#endif
#include "global.h"

#include "libeg.h"

#include "pointer.h"

#define MENU_EXIT_ENTER   (1)
#define MENU_EXIT_ESCAPE  (2)
#define MENU_EXIT_DETAILS (3)
#define MENU_EXIT_TIMEOUT (4)
#define MENU_EXIT_EJECT   (5)
#define MENU_EXIT_HIDE    (6)

#define TAG_RETURN       (99)
/*
 * CurrentSelection indexes the whole Entries array. FirstVisible and
 * LastVisible describe the displayed interval. Keeping both coordinate
 * systems lets keyboard selection survive scrolling and lets hit testing
 * recover the original entry index.
 */
typedef struct {
   INTN CurrentSelection, PreviousSelection, MaxIndex;
   INTN FirstVisible, LastVisible, MaxVisible;
   INTN FinalRow0, InitialRow1;
   INTN ScrollMode;
   BOOLEAN PaintAll, PaintSelection;
} SCROLL_STATE;

#define SCROLL_LINE_UP    (0)
#define SCROLL_LINE_DOWN  (1)
#define SCROLL_PAGE_UP    (2)
#define SCROLL_PAGE_DOWN  (3)
#define SCROLL_FIRST      (4)
#define SCROLL_LAST       (5)
#define SCROLL_NONE       (6)
#define SCROLL_LINE_RIGHT (7)
#define SCROLL_LINE_LEFT  (8)

#define SCROLL_MODE_TEXT  (0)  
#define SCROLL_MODE_ICONS (1)  

#define POINTER_NO_ITEM     (-1)
#define POINTER_LEFT_ARROW  (-2)
#define POINTER_RIGHT_ARROW (-3)

#define INPUT_KEY         (0)
#define INPUT_POINTER     (1)
#define INPUT_TIMEOUT     (2)
#define INPUT_TIMER_ERROR (3)

#define MAX_LINE_LENGTH 65

struct _boot_menu_screen;

/*
 * Style callbacks share Screen and State across initialisation, repaint,
 * selection changes and cleanup. Entries must outlive this lifecycle; a rescan
 * invalidates pointers and indices.
 */
typedef VOID (*MENU_STYLE_FUNC)(IN BOOT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, IN UINTN Function, IN CHAR16 *ParamText);

/*
 * Append borrowed record pointers, not deep copies. Strings and nested
 * entries must outlive the screen; an entry selected from the old inventory
 * is invalid following ScanForBootloaders. Neither append reports OOM.
 */
VOID AddMenuInfoLine(IN BOOT_MENU_SCREEN *Screen, IN CHAR16 *InfoLine);
VOID AddMenuEntry(IN BOOT_MENU_SCREEN *Screen, IN BOOT_MENU_ENTRY *Entry);
UINTN ComputeRow0PosY(VOID);
VOID MainMenuStyle(IN BOOT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, IN UINTN Function, IN CHAR16 *ParamText);
UINTN RunMenu(IN BOOT_MENU_SCREEN *Screen, OUT BOOT_MENU_ENTRY **ChosenEntry);
VOID DisplaySimpleMessage(CHAR16 *Title, CHAR16 *Message);
VOID TextMenuStyle(IN BOOT_MENU_SCREEN *Screen,
                   IN SCROLL_STATE *State,
                   IN UINTN Function,
                   IN CHAR16 *ParamText);
VOID GraphicsMenuStyle(IN BOOT_MENU_SCREEN *Screen,
                       IN SCROLL_STATE *State,
                       IN UINTN Function,
                       IN CHAR16 *ParamText);
/*
 * Return a MENU_EXIT_* UI action and a pointer borrowed from Screen.
 * DefaultEntryIndex is an array index updated by input/scroll handling; it
 * is not a persistent firmware boot option number.
 */
UINTN RunGenericMenu(IN BOOT_MENU_SCREEN *Screen,
                     IN MENU_STYLE_FUNC StyleFunc,
                     IN OUT INTN *DefaultEntryIndex,
                     OUT BOOT_MENU_ENTRY **ChosenEntry);
VOID ManageHiddenTags(VOID);
CHAR16* ReadHiddenTags(CHAR16 *VarName);
/*
 * Nested menu selection can return a child rather than a top-level entry.
 * The returned pointer belongs to the current scan and its Tag controls
 * downstream EFI application versus firmware-variable dispatch.
 */
UINTN RunMainMenu(IN BOOT_MENU_SCREEN *Screen, IN CHAR16** DefaultSelection, OUT BOOT_MENU_ENTRY **ChosenEntry);
UINTN FindMainMenuItem(IN BOOT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, IN UINTN PosX, IN UINTN PosY);
/*
 * GenerateWaitList allocates the keyboard/pointer event list; WaitForInput
 * supplies a timer where requested. INPUT_* categories describe input source,
 * not a successful loader handoff.
 */
VOID GenerateWaitList();
UINTN WaitForInput(IN UINTN Timeout);

#endif
