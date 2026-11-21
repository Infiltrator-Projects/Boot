/*
 * Menu entry, scrolling, input event and action-result contract.
 * MENU_EXIT_* results describe UI transitions, not persistent boot-record
 * operations. The screen's selected entry must belong to the current scan;
 * bootloader image validity remains an independent responsibility.
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

struct _refit_menu_screen;

typedef VOID (*MENU_STYLE_FUNC)(IN REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, IN UINTN Function, IN CHAR16 *ParamText);

VOID AddMenuInfoLine(IN REFIT_MENU_SCREEN *Screen, IN CHAR16 *InfoLine);
VOID AddMenuEntry(IN REFIT_MENU_SCREEN *Screen, IN REFIT_MENU_ENTRY *Entry);
UINTN ComputeRow0PosY(VOID);
VOID MainMenuStyle(IN REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, IN UINTN Function, IN CHAR16 *ParamText);
UINTN RunMenu(IN REFIT_MENU_SCREEN *Screen, OUT REFIT_MENU_ENTRY **ChosenEntry);
VOID DisplaySimpleMessage(CHAR16 *Title, CHAR16 *Message);
VOID TextMenuStyle(IN REFIT_MENU_SCREEN *Screen,
                   IN SCROLL_STATE *State,
                   IN UINTN Function,
                   IN CHAR16 *ParamText);
VOID GraphicsMenuStyle(IN REFIT_MENU_SCREEN *Screen,
                       IN SCROLL_STATE *State,
                       IN UINTN Function,
                       IN CHAR16 *ParamText);
UINTN RunGenericMenu(IN REFIT_MENU_SCREEN *Screen,
                     IN MENU_STYLE_FUNC StyleFunc,
                     IN OUT INTN *DefaultEntryIndex,
                     OUT REFIT_MENU_ENTRY **ChosenEntry);
VOID ManageHiddenTags(VOID);
CHAR16* ReadHiddenTags(CHAR16 *VarName);
UINTN RunMainMenu(IN REFIT_MENU_SCREEN *Screen, IN CHAR16** DefaultSelection, OUT REFIT_MENU_ENTRY **ChosenEntry);
UINTN FindMainMenuItem(IN REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, IN UINTN PosX, IN UINTN PosY);
VOID GenerateWaitList();
UINTN WaitForInput(IN UINTN Timeout);

#endif

 
