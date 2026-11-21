/*
 * Firmware console/UI rendering, input and entry-selection state.
 * Rows, selection highlights, hidden-tag records and the default match
 * control menu presentation, not physical installation or EFI BootOrder.
 * The graphics fallback must never compromise a viable boot choice.
 */

#include "global.h"
#include "screen.h"
#include "lib.h"
#include "log.h"
#include "menu.h"
#include "config.h"
#include "libeg.h"
#include "libegint.h"
#include "line_edit.h"
#include "mystrings.h"
#include "icns.h"
#include "scan.h"
#include "../include/refit_call_wrapper.h"

#include "../include/egemb_back_selected_small.h"
#include "../include/egemb_back_selected_big.h"
#include "../include/egemb_arrow_left.h"
#include "../include/egemb_arrow_right.h"

#define MENU_FUNCTION_INIT            (0)
#define MENU_FUNCTION_CLEANUP         (1)
#define MENU_FUNCTION_PAINT_ALL       (2)
#define MENU_FUNCTION_PAINT_SELECTION (3)
#define MENU_FUNCTION_PAINT_TIMEOUT   (4)
#define MENU_FUNCTION_PAINT_HINTS     (5)

static CHAR16 ArrowUp[2] = { ARROW_UP, 0 };
static CHAR16 ArrowDown[2] = { ARROW_DOWN, 0 };
static UINTN TileSizes[2] = { 144, 64 };

#define TEXT_YMARGIN (2)
#define TITLEICON_SPACING (16)

#define TILE_XSPACING (8)
#define TILE_YSPACING (16)

static EG_IMAGE *SelectionImages[2] = { NULL, NULL };
static EG_PIXEL SelectionBackgroundPixel = { 0xff, 0xff, 0xff, 0 };

EFI_EVENT* WaitList = NULL;
UINTN WaitListLength = 0;

BOOLEAN PointerEnabled = FALSE;
BOOLEAN PointerActive = FALSE;
BOOLEAN DrawSelection = TRUE;

extern EFI_GUID         BootGuid;
extern REFIT_MENU_ENTRY MenuEntryReturn;
static REFIT_MENU_ENTRY MenuEntryYes = { L"Yes", TAG_RETURN, 1, 0, 0, NULL, NULL, NULL };
static REFIT_MENU_ENTRY MenuEntryNo = { L"No", TAG_RETURN, 1, 0, 0, NULL, NULL, NULL };

static VOID InitSelection(VOID)
{
    EG_IMAGE    *TempSmallImage = NULL, *TempBigImage = NULL;
    BOOLEAN     LoadedSmallImage = FALSE;

    if (!AllowGraphicsMode)
        return;
    if (SelectionImages[0] != NULL)
        return;

    if (GlobalConfig.SelectionSmallFileName != NULL) {
        TempSmallImage = egLoadImage(SelfDir, GlobalConfig.SelectionSmallFileName, TRUE);
    }
    if (TempSmallImage == NULL)
        TempSmallImage = egPrepareEmbeddedImage(&egemb_back_selected_small, TRUE);
    else
        LoadedSmallImage = TRUE;
    SelectionImages[1] = egScaleImage(TempSmallImage, TileSizes[1], TileSizes[1]);

    if (GlobalConfig.SelectionBigFileName != NULL) {
        TempBigImage = egLoadImage(SelfDir, GlobalConfig.SelectionBigFileName, TRUE);
    }
    if (TempBigImage == NULL) {
        if (LoadedSmallImage) {

            TempBigImage = egCopyImage(TempSmallImage);
        } else {
            TempBigImage = egPrepareEmbeddedImage(&egemb_back_selected_big, TRUE);
        }
    }
    SelectionImages[0] = egScaleImage(TempBigImage, TileSizes[0], TileSizes[0]);

    if (TempSmallImage)
        egFreeImage(TempSmallImage);
    if (TempBigImage)
        egFreeImage(TempBigImage);
}  

static VOID InitScroll(OUT SCROLL_STATE *State, IN UINTN ItemCount, IN UINTN VisibleSpace)
{
    State->PreviousSelection = State->CurrentSelection = 0;
    State->MaxIndex = (INTN)ItemCount - 1;
    State->FirstVisible = 0;
    if (AllowGraphicsMode) {
        State->MaxVisible = UGAWidth / (TileSizes[0] + TILE_XSPACING) - 1;
    } else
        State->MaxVisible = ConHeight - 4;
    if ((VisibleSpace > 0) && (VisibleSpace < State->MaxVisible))
        State->MaxVisible = (INTN)VisibleSpace;
    State->PaintAll = TRUE;
    State->PaintSelection = FALSE;

    State->LastVisible = State->FirstVisible + State->MaxVisible - 1;
}

/*
 * Reconciles cursor and visible-window bounds after menu geometry changes.
 * Scroll state is UI-local; its indices must not be interpreted as stable
 * identifiers for boot entries after a rescan.
 */
static VOID AdjustScrollState(IN SCROLL_STATE *State) {
    if (State->CurrentSelection > State->LastVisible) {
        State->LastVisible = State->CurrentSelection;
        State->FirstVisible = 1 + State->CurrentSelection - State->MaxVisible;
        if (State->FirstVisible < 0)  
            State->FirstVisible = 0;
        State->PaintAll = TRUE;
    }  
    if (State->CurrentSelection < State->FirstVisible) {
        State->FirstVisible = State->CurrentSelection;
        State->LastVisible = State->CurrentSelection + State->MaxVisible - 1;
        State->PaintAll = TRUE;
    }  
}  

/**
 * Preserve the old selection for a two-item repaint. In icon mode a vertical move
 * projects the selection's relative horizontal position from one row onto the
 * other using integer arithmetic. A single visible source item uses a special
 * case to avoid dividing by zero. Text mode instead advances by item and adjusts
 * its viewport.
 */
static VOID UpdateScroll(IN OUT SCROLL_STATE *State, IN UINTN Movement)
{
    State->PreviousSelection = State->CurrentSelection;

    switch (Movement) {
        case SCROLL_LINE_LEFT:
            if (State->CurrentSelection > 0) {
                State->CurrentSelection --;
            }
            break;

        case SCROLL_LINE_RIGHT:
            if (State->CurrentSelection < State->MaxIndex) {
                State->CurrentSelection ++;
            }
            break;

        case SCROLL_LINE_UP:
            if (State->ScrollMode == SCROLL_MODE_ICONS) {
               if (State->CurrentSelection >= State->InitialRow1) {
                  if (State->MaxIndex > State->InitialRow1) {  
                     State->CurrentSelection = State->FirstVisible + (State->LastVisible - State->FirstVisible) *
                                               (State->CurrentSelection - State->InitialRow1) /
                                               (State->MaxIndex - State->InitialRow1);
                  } else {
                     State->CurrentSelection = State->FirstVisible;
                  }  
               }  
            } else {
               if (State->CurrentSelection > 0)
                  State->CurrentSelection--;
            }  
            break;

        case SCROLL_LINE_DOWN:
           if (State->ScrollMode == SCROLL_MODE_ICONS) {
               if (State->CurrentSelection <= State->FinalRow0) {
                  if (State->LastVisible > State->FirstVisible) {  
                     State->CurrentSelection = State->InitialRow1 + (State->MaxIndex - State->InitialRow1) *
                                               (State->CurrentSelection - State->FirstVisible) /
                                               (State->LastVisible - State->FirstVisible);
                  } else {
                     State->CurrentSelection = State->InitialRow1;
                  }  
               }  
            } else {
               if (State->CurrentSelection < State->MaxIndex)
                  State->CurrentSelection++;
            }  
            break;

        case SCROLL_PAGE_UP:
           if (State->CurrentSelection <= State->FinalRow0)
              State->CurrentSelection -= State->MaxVisible;
           else if (State->CurrentSelection == State->InitialRow1)
              State->CurrentSelection = State->FinalRow0;
           else
              State->CurrentSelection = State->InitialRow1;
           if (State->CurrentSelection < 0)
              State->CurrentSelection = 0;
           break;

        case SCROLL_FIRST:
           if (State->CurrentSelection > 0) {
              State->PaintAll = TRUE;
              State->CurrentSelection = 0;
           }
           break;

        case SCROLL_PAGE_DOWN:
           if (State->CurrentSelection < State->FinalRow0) {
              State->CurrentSelection += State->MaxVisible;
              if (State->CurrentSelection > State->FinalRow0)
                 State->CurrentSelection = State->FinalRow0;
           } else if (State->CurrentSelection == State->FinalRow0) {
              State->CurrentSelection++;
           } else {
              State->CurrentSelection = State->MaxIndex;
           }
           if (State->CurrentSelection > State->MaxIndex)
              State->CurrentSelection = State->MaxIndex;
           break;

        case SCROLL_LAST:
           if (State->CurrentSelection < State->MaxIndex) {
              State->PaintAll = TRUE;
              State->CurrentSelection = State->MaxIndex;
           }
           break;

        case SCROLL_NONE:
            break;

    }
    if (State->ScrollMode == SCROLL_MODE_TEXT)
        AdjustScrollState(State);

    if (!State->PaintAll && State->CurrentSelection != State->PreviousSelection)
        State->PaintSelection = TRUE;
    State->LastVisible = State->FirstVisible + State->MaxVisible - 1;
}  

VOID AddMenuInfoLine(IN REFIT_MENU_SCREEN *Screen, IN CHAR16 *InfoLine)
{
    LOG(3, LOG_LINE_NORMAL, L"Adding menu info line: '%s'", InfoLine);
    AddListElement((VOID ***) &(Screen->InfoLines), &(Screen->InfoLineCount), InfoLine);
}

VOID AddMenuEntry(IN REFIT_MENU_SCREEN *Screen, IN REFIT_MENU_ENTRY *Entry)
{
    LOG(3, LOG_LINE_NORMAL, L"Adding menu entry for '%s'", Entry->Title);
    AddListElement((VOID ***) &(Screen->Entries), &(Screen->EntryCount), Entry);
}

/**
 * Resolve the comma-separated defaults left to right. A single character is
 * matched against assigned shortcut keys, while a longer token is sought within
 * each displayed title. Both searches stop at the first match. Return -1 when all
 * preferences are exhausted.
 */
static INTN FindMenuShortcutEntry(IN REFIT_MENU_SCREEN *Screen, IN CHAR16 *Defaults) {
    UINTN i, j = 0, ShortcutLength;
    CHAR16 *Shortcut;

    while ((Shortcut = FindCommaDelimited(Defaults, j)) != NULL) {
        ShortcutLength = StrLen(Shortcut);
        if (ShortcutLength == 1) {
            if (Shortcut[0] >= 'a' && Shortcut[0] <= 'z')
                Shortcut[0] -= ('a' - 'A');
            if (Shortcut[0]) {
                for (i = 0; i < Screen->EntryCount; i++) {
                    if (Screen->Entries[i]->ShortcutDigit == Shortcut[0] ||
                        Screen->Entries[i]->ShortcutLetter == Shortcut[0]) {
                            MyFreePool(Shortcut);
                            return i;
                    }  
                }  
            }  
        } else if (ShortcutLength > 1) {
            for (i = 0; i < Screen->EntryCount; i++) {
                if (StriSubCmp(Shortcut, Screen->Entries[i]->Title)) {
                    MyFreePool(Shortcut);
                    return i;
                }  
            }  
        }
        MyFreePool(Shortcut);
        j++;
    }  
    return -1;
}  

/**
 * Record the last index on row zero and the first on row one. The painter and
 * keyboard projection treat the rows as contiguous index ranges, as produced by
 * adding loaders before tools. Icon mode also caps the visible loader count to
 * the available row-zero items.
 */
static VOID IdentifyRows(IN SCROLL_STATE *State, IN REFIT_MENU_SCREEN *Screen) {
    UINTN i;

    State->FinalRow0 = 0;
    State->InitialRow1 = State->MaxIndex;
    for (i = 0; i <= State->MaxIndex; i++) {
        if (Screen->Entries[i]->Row == 0) {
            State->FinalRow0 = i;
        } else if ((Screen->Entries[i]->Row == 1) && (State->InitialRow1 > i)) {
            State->InitialRow1 = i;
        }  
    }  
    if ((State->ScrollMode == SCROLL_MODE_ICONS) && (State->MaxVisible > (State->FinalRow0 + 1)))
        State->MaxVisible = State->FinalRow0 + 1;
}  

static VOID SaveScreen(VOID) {
    EG_PIXEL Black = { 0x0, 0x0, 0x0, 0 };

    egClearScreen(&Black);

    WaitForInput(0);

    if (AllowGraphicsMode)
        SwitchToGraphicsAndClear();
    ReadAllKeyStrokes();
}  

/**
 * Return a menu-exit reason together with the selected entry borrowed from
 * Screen. Keyboard and pointer activity cancel a positive timeout. Countdown is
 * stored in tenths of a second, while WaitForInput receives milliseconds and the
 * displayed label is rounded to whole seconds.
 *
 * TimeoutSeconds equal to the unsigned representation of -1 requests an immediate
 * decision using the pending keystroke. A zero timeout takes the ordinary
 * interactive path. Both cases differ from a positive countdown expiring.
 */
UINTN RunGenericMenu(IN REFIT_MENU_SCREEN *Screen,
                     IN MENU_STYLE_FUNC StyleFunc,
                     IN OUT INTN *DefaultEntryIndex,
                     OUT REFIT_MENU_ENTRY **ChosenEntry) {
    SCROLL_STATE State;
    EFI_STATUS Status;
    EFI_INPUT_KEY key;
    INTN ShortcutEntry;
    BOOLEAN HaveTimeout = FALSE;
    BOOLEAN WaitForRelease = FALSE;
    UINTN TimeoutCountdown = 0;
    INTN PreviousTime = -1, CurrentTime, TimeSinceKeystroke = 0;
    CHAR16 TimeoutMessage[256];
    CHAR16 KeyAsString[2];
    UINTN MenuExit;
    EFI_STATUS PointerStatus = EFI_NOT_READY;
    UINTN Item;

    LOG(2, LOG_LINE_NORMAL, L"Running menu screen: '%s'", Screen->Title);
    if (Screen->TimeoutSeconds > 0) {
        HaveTimeout = TRUE;
        TimeoutCountdown = Screen->TimeoutSeconds * 10;
    }
    MenuExit = 0;

    StyleFunc(Screen, &State, MENU_FUNCTION_INIT, NULL);
    IdentifyRows(&State, Screen);

    if (*DefaultEntryIndex >= 0 && *DefaultEntryIndex <= State.MaxIndex) {
        State.CurrentSelection = *DefaultEntryIndex;
        if (GlobalConfig.ScreensaverTime != -1)
           UpdateScroll(&State, SCROLL_NONE);
    }

    if (Screen->TimeoutSeconds == -1) {
        Status = refit_call2_wrapper(ST->ConIn->ReadKeyStroke, ST->ConIn, &key);
        if (Status == EFI_SUCCESS) {
            KeyAsString[0] = key.UnicodeChar;
            KeyAsString[1] = 0;
            ShortcutEntry = FindMenuShortcutEntry(Screen, KeyAsString);
            if (ShortcutEntry >= 0) {
                State.CurrentSelection = ShortcutEntry;
                MenuExit = MENU_EXIT_ENTER;
            } else {
                WaitForRelease = TRUE;
                HaveTimeout = FALSE;
            }
        } else {
            MenuExit = MENU_EXIT_TIMEOUT;
        }
    }

    if (GlobalConfig.ScreensaverTime != -1)
        State.PaintAll = TRUE;

    LOG(3, LOG_LINE_NORMAL, L"About to enter while() loop in RunGenericMenu()");
    while (!MenuExit) {

        pdClear();
        if (State.PaintAll && (GlobalConfig.ScreensaverTime != -1)) {
            StyleFunc(Screen, &State, MENU_FUNCTION_PAINT_ALL, NULL);
            State.PaintAll = FALSE;
        } else if (State.PaintSelection) {
            StyleFunc(Screen, &State, MENU_FUNCTION_PAINT_SELECTION, NULL);
            State.PaintSelection = FALSE;
        }
        pdDraw();

        if (WaitForRelease) {
            Status = refit_call2_wrapper(ST->ConIn->ReadKeyStroke, ST->ConIn, &key);
            if (Status == EFI_SUCCESS) {

                refit_call2_wrapper(ST->ConIn->Reset, ST->ConIn, FALSE);
                refit_call1_wrapper(BS->Stall, 100000);
            } else {
                WaitForRelease = FALSE;
                refit_call2_wrapper(ST->ConIn->Reset, ST->ConIn, TRUE);
            }
            continue;
        }

        if (HaveTimeout) {
            CurrentTime = (TimeoutCountdown + 5) / 10;
            if (CurrentTime != PreviousTime) {
               SPrint(TimeoutMessage, 255, L"%s in %d seconds", Screen->TimeoutText, CurrentTime);
               if (GlobalConfig.ScreensaverTime != -1)
                  StyleFunc(Screen, &State, MENU_FUNCTION_PAINT_TIMEOUT, TimeoutMessage);
               PreviousTime = CurrentTime;
            }
        }

        if (PointerEnabled) {
            PointerStatus = pdUpdateState();
        }
        Status = refit_call2_wrapper(ST->ConIn->ReadKeyStroke, ST->ConIn, &key);

        if (Status == EFI_SUCCESS) {
            PointerActive = FALSE;
            DrawSelection = TRUE;
            TimeSinceKeystroke = 0;
        } else if (PointerStatus == EFI_SUCCESS) {
            if (StyleFunc != MainMenuStyle && pdGetState().Press) {

                MenuExit = MENU_EXIT_ENTER;
                break;
            }

            PointerActive = TRUE;
            TimeSinceKeystroke = 0;
        } else {
            if (HaveTimeout && TimeoutCountdown == 0) {

                LOG(1, LOG_LINE_NORMAL, L"Menu timeout expired");
                MenuExit = MENU_EXIT_TIMEOUT;
                break;
            } else if (HaveTimeout || GlobalConfig.ScreensaverTime > 0) {
                UINTN ElapsCount = 1;

                UINTN Input = WaitForInput(1000);  

                if (Input == INPUT_KEY || Input == INPUT_POINTER) {
                    continue;
                } else if (Input == INPUT_TIMEOUT) {
                    ElapsCount = 10;  
                }

                TimeSinceKeystroke += ElapsCount;
                if (HaveTimeout) {
                    TimeoutCountdown = TimeoutCountdown <= ElapsCount ? 0 : TimeoutCountdown - ElapsCount;
                } else if (GlobalConfig.ScreensaverTime > 0 &&
                    TimeSinceKeystroke > (GlobalConfig.ScreensaverTime * 10))
                {
                    SaveScreen();
                    State.PaintAll = TRUE;
                    TimeSinceKeystroke = 0;
                }  
            } else {
                WaitForInput(0);
            }
            continue;
        }  

        if (HaveTimeout) {

            StyleFunc(Screen, &State, MENU_FUNCTION_PAINT_TIMEOUT, L"");
            HaveTimeout = FALSE;
            if (GlobalConfig.ScreensaverTime == -1) {  
               GlobalConfig.ScreensaverTime = 0;
               if (!GlobalConfig.TextOnly)
                 BltClearScreen(TRUE);
            }
        }

        if (!PointerActive) {  
            LOG(3, LOG_LINE_NORMAL, L"Processing keystroke (ScanCode = %d)....", key.ScanCode);
            switch (key.ScanCode) {
                case SCAN_UP:
                    UpdateScroll(&State, SCROLL_LINE_UP);
                    break;
                case SCAN_LEFT:
                    UpdateScroll(&State, SCROLL_LINE_LEFT);
                    break;
                case SCAN_DOWN:
                    UpdateScroll(&State, SCROLL_LINE_DOWN);
                    break;
                case SCAN_RIGHT:
                    UpdateScroll(&State, SCROLL_LINE_RIGHT);
                    break;
                case SCAN_HOME:
                    UpdateScroll(&State, SCROLL_FIRST);
                    break;
                case SCAN_END:
                    UpdateScroll(&State, SCROLL_LAST);
                    break;
                case SCAN_PAGE_UP:
                    UpdateScroll(&State, SCROLL_PAGE_UP);
                    break;
                case SCAN_PAGE_DOWN:
                    UpdateScroll(&State, SCROLL_PAGE_DOWN);
                    break;
                case SCAN_ESC:
                    MenuExit = MENU_EXIT_ESCAPE;
                    break;
                case SCAN_INSERT:
                case SCAN_F2:
                    MenuExit = MENU_EXIT_DETAILS;
                    break;
                case SCAN_DELETE:
                    MenuExit = MENU_EXIT_HIDE;
                    break;
                case SCAN_F10:
                    egScreenShot();
                    break;
                case 0x0016:  
                    if (EjectMedia())
                        MenuExit = MENU_EXIT_ESCAPE;
                    break;
            }
            LOG(3, LOG_LINE_NORMAL, L"Processing keystroke (UnicodeChar = %d)....", key.UnicodeChar);
            switch (key.UnicodeChar) {
                case CHAR_LINEFEED:
                case CHAR_CARRIAGE_RETURN:
                case ' ':
                    MenuExit = MENU_EXIT_ENTER;
                    break;
                case CHAR_BACKSPACE:
                    MenuExit = MENU_EXIT_ESCAPE;
                    break;
                case '+':
                case CHAR_TAB:
                    MenuExit = MENU_EXIT_DETAILS;
                    break;
                case '-':
                    MenuExit = MENU_EXIT_HIDE;
                    break;
                case '\\':
                    egScreenShot();
                    break;
                default:
                    KeyAsString[0] = key.UnicodeChar;
                    KeyAsString[1] = 0;
                    ShortcutEntry = FindMenuShortcutEntry(Screen, KeyAsString);
                    if (ShortcutEntry >= 0) {
                        State.CurrentSelection = ShortcutEntry;
                        MenuExit = MENU_EXIT_ENTER;
                    }
                    break;
            }
        } else {  
            LOG(3, LOG_LINE_NORMAL, L"Processing pointer event");
            if (StyleFunc != MainMenuStyle) {
                continue;  
            }
            State.PreviousSelection = State.CurrentSelection;
            POINTER_STATE PointerState = pdGetState();
            Item = FindMainMenuItem(Screen, &State, PointerState.X, PointerState.Y);
            switch (Item) {
                case POINTER_NO_ITEM:
                    if(DrawSelection) {
                        DrawSelection = FALSE;
                        State.PaintSelection = TRUE;
                    }
                    break;
                case POINTER_LEFT_ARROW:
                    if(PointerState.Press) {
                        UpdateScroll(&State, SCROLL_PAGE_UP);
                    }
                    if(DrawSelection) {
                        DrawSelection = FALSE;
                        State.PaintSelection = TRUE;
                    }
                    break;
                case POINTER_RIGHT_ARROW:
                    if(PointerState.Press) {
                        UpdateScroll(&State, SCROLL_PAGE_DOWN);
                    }
                    if(DrawSelection) {
                        DrawSelection = FALSE;
                        State.PaintSelection = TRUE;
                    }
                    break;
                default:
                    if (!DrawSelection || Item != State.CurrentSelection) {
                        DrawSelection = TRUE;
                        State.PaintSelection = TRUE;
                        State.CurrentSelection = Item;
                    }
                    if(PointerState.Press) {
                        MenuExit = MENU_EXIT_ENTER;
                    }
                    break;
            }  
        }  
    }  

    pdClear();
    StyleFunc(Screen, &State, MENU_FUNCTION_CLEANUP, NULL);

    if (ChosenEntry)
        *ChosenEntry = Screen->Entries[State.CurrentSelection];
    *DefaultEntryIndex = State.CurrentSelection;
    LOG(3, LOG_LINE_NORMAL, L"Returning %d from RunGenericMenu()", MenuExit);
    return MenuExit;
}  

static VOID ShowTextInfoLines(IN REFIT_MENU_SCREEN *Screen) {
    INTN i;

    BeginTextScreen(Screen->Title);
    if (Screen->InfoLineCount > 0) {
        refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
        for (i = 0; i < (INTN)Screen->InfoLineCount; i++) {
            refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 3, 4 + i);
            refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, Screen->InfoLines[i]);
        }
    }
}  

VOID TextMenuStyle(IN REFIT_MENU_SCREEN *Screen,
                   IN SCROLL_STATE *State,
                   IN UINTN Function,
                   IN CHAR16 *ParamText)
{
    INTN i;
    UINTN MenuWidth, ItemWidth, MenuHeight;
    static UINTN MenuPosY;
    static CHAR16 **DisplayStrings;
    CHAR16 TimeoutMessage[256];

    State->ScrollMode = SCROLL_MODE_TEXT;
    switch (Function) {

        case MENU_FUNCTION_INIT:

            MenuPosY = 4;
            if (Screen->InfoLineCount > 0)
                MenuPosY += Screen->InfoLineCount + 1;
            MenuHeight = ConHeight - MenuPosY - 3;
            if (Screen->TimeoutSeconds > 0)
                MenuHeight -= 2;
            InitScroll(State, Screen->EntryCount, MenuHeight);

            MenuWidth = 20;   
            for (i = 0; i <= State->MaxIndex; i++) {
                ItemWidth = StrLen(Screen->Entries[i]->Title);
                if (MenuWidth < ItemWidth)
                    MenuWidth = ItemWidth;
            }
            MenuWidth += 2;
            if (MenuWidth > ConWidth - 3)
                MenuWidth = ConWidth - 3;

            DisplayStrings = AllocatePool(sizeof(CHAR16 *) * Screen->EntryCount);
            for (i = 0; i <= State->MaxIndex; i++) {

                DisplayStrings[i] = AllocateZeroPool(2 * sizeof(CHAR16));
                DisplayStrings[i][0] = L' ';
                MergeStrings(&DisplayStrings[i], Screen->Entries[i]->Title, 0);
                if (StrLen(DisplayStrings[i]) > MenuWidth)
                   DisplayStrings[i][MenuWidth - 1] = 0;

            }  

            break;

        case MENU_FUNCTION_CLEANUP:

            for (i = 0; i <= State->MaxIndex; i++)
                MyFreePool(DisplayStrings[i]);
            MyFreePool(DisplayStrings);
            break;

        case MENU_FUNCTION_PAINT_ALL:

            ShowTextInfoLines(Screen);
            for (i = 0; i <= State->MaxIndex; i++) {
                if (i >= State->FirstVisible && i <= State->LastVisible) {
                    refit_call3_wrapper(ST->ConOut->SetCursorPosition,
                                        ST->ConOut,
                                        2,
                                        MenuPosY + (i - State->FirstVisible));
                    if (i == State->CurrentSelection)
                       refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_CHOICE_CURRENT);
                    else
                       refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_CHOICE_BASIC);
                    refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, DisplayStrings[i]);
                }
            }

            refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_SCROLLARROW);
            refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 0, MenuPosY);
            if (State->FirstVisible > 0)
                refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, ArrowUp);
            else
                refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, L" ");
            refit_call3_wrapper(ST->ConOut->SetCursorPosition,
                                ST->ConOut,
                                0,
                                MenuPosY + State->MaxVisible);
            if (State->LastVisible < State->MaxIndex)
                refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, ArrowDown);
            else
                refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, L" ");
            if (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_HINTS)) {
                if (Screen->Hint1 != NULL) {
                    refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 0, ConHeight - 2);
                    refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, Screen->Hint1);
                }
                if (Screen->Hint2 != NULL) {
                    refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 0, ConHeight - 1);
                    refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, Screen->Hint2);
                }
            }
            break;

        case MENU_FUNCTION_PAINT_SELECTION:

            refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 2,
                                MenuPosY + (State->PreviousSelection - State->FirstVisible));
            refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_CHOICE_BASIC);
            refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, DisplayStrings[State->PreviousSelection]);
            refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 2,
                                MenuPosY + (State->CurrentSelection - State->FirstVisible));
            refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_CHOICE_CURRENT);
            refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, DisplayStrings[State->CurrentSelection]);
            break;

        case MENU_FUNCTION_PAINT_TIMEOUT:
            if (ParamText[0] == 0) {

                refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_BASIC);
                refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 0, ConHeight - 3);
                refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, BlankLine + 1);
            } else {

                refit_call2_wrapper(ST->ConOut->SetAttribute, ST->ConOut, ATTR_ERROR);
                refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 3, ConHeight - 3);
                SPrint(TimeoutMessage, 255, L"%s  ", ParamText);
                refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, TimeoutMessage);
            }
            break;
    }
}

inline static UINTN TextLineHeight(VOID) {
    return egGetFontHeight() + TEXT_YMARGIN * 2;
}  

static VOID DrawText(IN CHAR16 *Text, IN BOOLEAN Selected, IN UINTN FieldWidth, IN UINTN XPos, IN UINTN YPos)
{
    EG_IMAGE *TextBuffer;
    EG_PIXEL Bg;

    TextBuffer = egCreateFilledImage(FieldWidth, TextLineHeight(), FALSE, &MenuBackgroundPixel);
    if (TextBuffer) {
        Bg = MenuBackgroundPixel;
        if (Selected) {

            egFillImageArea(TextBuffer, 0, 0, FieldWidth, TextBuffer->Height, &SelectionBackgroundPixel);
            Bg = SelectionBackgroundPixel;
        }

        egRenderText(Text, TextBuffer, egGetFontCellWidth(), TEXT_YMARGIN, (Bg.r + Bg.g + Bg.b) / 3);
        egDrawImageWithTransparency(TextBuffer, NULL, XPos, YPos, TextBuffer->Width, TextBuffer->Height);
        egFreeImage(TextBuffer);
    }
}  

static UINT8 AverageBrightness(EG_IMAGE *Image) {
    UINTN i;
    UINTN Sum = 0;

    if ((Image != NULL) && ((Image->Width * Image->Height) != 0)) {
        for (i = 0; i < (Image->Width * Image->Height); i++) {
            Sum += (Image->PixelData[i].r + Image->PixelData[i].g + Image->PixelData[i].b);
        }
        Sum /= (Image->Width * Image->Height * 3);
    }  
    return (UINT8) Sum;
}  

static VOID DrawTextWithTransparency(IN CHAR16 *Text, IN UINTN XPos, IN UINTN YPos)
{
    UINTN TextWidth;
    EG_IMAGE *TextBuffer = NULL;

    if (Text == NULL)
       Text = L"";

    egMeasureText(Text, &TextWidth, NULL);
    if (TextWidth == 0) {
       TextWidth = UGAWidth;
       XPos = 0;
    }

    TextBuffer = egCropImage(GlobalConfig.ScreenBackground, XPos, YPos, TextWidth, TextLineHeight());
    if (TextBuffer == NULL)
       return;

    egRenderText(Text, TextBuffer, 0, 0, AverageBrightness(TextBuffer));
    egDrawImageWithTransparency(TextBuffer, NULL, XPos, YPos, TextBuffer->Width, TextBuffer->Height);
    egFreeImage(TextBuffer);
}

static VOID ComputeSubScreenWindowSize(REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State,
                                       UINTN *XPos, UINTN *YPos,
                                       UINTN *Width, UINTN *Height, UINTN *LineWidth) {
    UINTN i, ItemWidth, HintTop, BannerBottomEdge, TitleWidth;
    UINTN FontCellWidth = egGetFontCellWidth();
    UINTN FontCellHeight = egGetFontHeight();

    *Width = 20;
    *Height = 5;
    TitleWidth = egComputeTextWidth(Screen->Title);

    for (i = 0; i < Screen->InfoLineCount; i++) {
         ItemWidth = StrLen(Screen->InfoLines[i]);
         if (*Width < ItemWidth) {
              *Width = ItemWidth;
         }
         (*Height)++;
    }
    for (i = 0; i <= State->MaxIndex; i++) {
         ItemWidth = StrLen(Screen->Entries[i]->Title);
         if (*Width < ItemWidth) {
              *Width = ItemWidth;
         }
         (*Height)++;
    }
    *Width = (*Width + 2) * FontCellWidth;
    *LineWidth = *Width;
    if (Screen->TitleImage)
        *Width += (Screen->TitleImage->Width + TITLEICON_SPACING * 2 + FontCellWidth);
    else
        *Width += FontCellWidth;

    if (*Width < TitleWidth)
        *Width = TitleWidth + 2 * FontCellWidth;

    if (*Width > UGAWidth)
        *Width = UGAWidth;

    *XPos = (UGAWidth - *Width) / 2;

    HintTop = UGAHeight - (FontCellHeight * 3);  
    *Height *= TextLineHeight();
    if (Screen->TitleImage && (*Height < (Screen->TitleImage->Height + TextLineHeight() * 4)))
        *Height = Screen->TitleImage->Height + TextLineHeight() * 4;

    if (GlobalConfig.BannerBottomEdge >= HintTop) {

        BannerBottomEdge = 0;
    } else {
        BannerBottomEdge = GlobalConfig.BannerBottomEdge;
    }
    if (*Height > (HintTop - BannerBottomEdge - FontCellHeight * 2)) {
        BannerBottomEdge = 0;
    }
    if (*Height > (HintTop - BannerBottomEdge - FontCellHeight * 2)) {

        *Height = (HintTop - BannerBottomEdge - FontCellHeight * 2);
    }

    *YPos = ((UGAHeight - *Height) / 2);
    if (*YPos < BannerBottomEdge)
        *YPos = BannerBottomEdge + FontCellHeight + (HintTop - BannerBottomEdge - *Height) / 2;
}  

/*
 * Renders a menu through UEFI graphics facilities using the current scroll
 * and selection state. Painting an icon must never be treated as a successful
 * validation or launch of the associated image.
 */
VOID GraphicsMenuStyle(IN REFIT_MENU_SCREEN *Screen,
                       IN SCROLL_STATE *State,
                       IN UINTN Function,
                       IN CHAR16 *ParamText)
{
    INTN i;
    UINTN ItemWidth;
    static UINTN LineWidth, MenuWidth, MenuHeight, EntriesPosX, TitlePosX, EntriesPosY, TimeoutPosY, CharWidth;
    EG_IMAGE *Window;
    EG_PIXEL *BackgroundPixel = &(GlobalConfig.ScreenBackground->PixelData[0]);

    CharWidth = egGetFontCellWidth();
    State->ScrollMode = SCROLL_MODE_TEXT;
    switch (Function) {

        case MENU_FUNCTION_INIT:
            InitScroll(State, Screen->EntryCount, 0);
            ComputeSubScreenWindowSize(Screen, State, &EntriesPosX, &EntriesPosY, &MenuWidth, &MenuHeight, &LineWidth);
            TimeoutPosY = EntriesPosY + (Screen->EntryCount + 1) * TextLineHeight();

            SwitchToGraphicsAndClear();
            Window = egCreateFilledImage(MenuWidth, MenuHeight, FALSE, BackgroundPixel);
            if (Window) {
                egDrawImage(Window, EntriesPosX, EntriesPosY);
                egFreeImage(Window);
            }
            ItemWidth = egComputeTextWidth(Screen->Title);
            if (MenuWidth > ItemWidth) {
               TitlePosX = EntriesPosX + (MenuWidth - ItemWidth) / 2 - CharWidth;
            } else {
               TitlePosX = EntriesPosX;
               if (CharWidth > 0) {
                  i = MenuWidth / CharWidth - 2;
                  if (i > 0)
                     Screen->Title[i] = 0;
               }  
            }  
            break;

        case MENU_FUNCTION_CLEANUP:

            break;

        case MENU_FUNCTION_PAINT_ALL:
           ComputeSubScreenWindowSize(Screen, State, &EntriesPosX, &EntriesPosY,
                                      &MenuWidth, &MenuHeight, &LineWidth);
           DrawText(Screen->Title, FALSE, (StrLen(Screen->Title) + 2) * CharWidth,
                    TitlePosX, EntriesPosY += TextLineHeight());
           if (Screen->TitleImage) {
              BltImageAlpha(Screen->TitleImage, EntriesPosX + TITLEICON_SPACING,
                            EntriesPosY + TextLineHeight() * 2,
                            BackgroundPixel);
              EntriesPosX += (Screen->TitleImage->Width + TITLEICON_SPACING * 2);
           }
           EntriesPosY += (TextLineHeight() * 2);
           if (Screen->InfoLineCount > 0) {
               for (i = 0; i < (INTN)Screen->InfoLineCount; i++) {
                   DrawText(Screen->InfoLines[i], FALSE, LineWidth, EntriesPosX, EntriesPosY);
                   EntriesPosY += TextLineHeight();
               }
               EntriesPosY += TextLineHeight();   
           }

           for (i = 0; i <= State->MaxIndex; i++) {
              DrawText(Screen->Entries[i]->Title, (i == State->CurrentSelection), LineWidth, EntriesPosX,
                       EntriesPosY + i * TextLineHeight());
           }
           if (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_HINTS)) {
              if ((Screen->Hint1 != NULL) && (StrLen(Screen->Hint1) > 0))
                 DrawTextWithTransparency(Screen->Hint1, (UGAWidth - egComputeTextWidth(Screen->Hint1)) / 2,
                                          UGAHeight - (egGetFontHeight() * 3));
              if ((Screen->Hint2 != NULL) && (StrLen(Screen->Hint2) > 0))
                 DrawTextWithTransparency(Screen->Hint2, (UGAWidth - egComputeTextWidth(Screen->Hint2)) / 2,
                                           UGAHeight - (egGetFontHeight() * 2));
           }  
           break;

        case MENU_FUNCTION_PAINT_SELECTION:

            DrawText(Screen->Entries[State->PreviousSelection]->Title, FALSE, LineWidth,
                     EntriesPosX, EntriesPosY + State->PreviousSelection * TextLineHeight());
            DrawText(Screen->Entries[State->CurrentSelection]->Title, TRUE, LineWidth,
                     EntriesPosX, EntriesPosY + State->CurrentSelection * TextLineHeight());
            break;

        case MENU_FUNCTION_PAINT_TIMEOUT:
            DrawText(ParamText, FALSE, LineWidth, EntriesPosX, TimeoutPosY);
            break;

    }
}  

static VOID DrawMainMenuEntry(REFIT_MENU_ENTRY *Entry, BOOLEAN selected, UINTN XPos, UINTN YPos)
{
    EG_IMAGE *Background;

    if (selected && DrawSelection) {
        Background = egCropImage(GlobalConfig.ScreenBackground, XPos, YPos,
                                 SelectionImages[Entry->Row]->Width, SelectionImages[Entry->Row]->Height);
        if (Background) {
            egComposeImage(Background, SelectionImages[Entry->Row], 0, 0);
            BltImageCompositeBadge(Background, Entry->Image, Entry->BadgeImage, XPos, YPos);
            egFreeImage(Background);
        }  
    } else {  
        egDrawImageWithTransparency(Entry->Image, Entry->BadgeImage, XPos, YPos,
                                    SelectionImages[Entry->Row]->Width, SelectionImages[Entry->Row]->Height);
    }  
}  

static VOID PaintAll(IN REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, UINTN *itemPosX,
                     UINTN row0PosY, UINTN row1PosY, UINTN textPosY) {
    INTN i;

    if (Screen->Entries[State->CurrentSelection]->Row == 0)
        AdjustScrollState(State);
    for (i = State->FirstVisible; i <= State->MaxIndex; i++) {
        if (Screen->Entries[i]->Row == 0) {
            if (i <= State->LastVisible) {
                DrawMainMenuEntry(Screen->Entries[i], (i == State->CurrentSelection) ? TRUE : FALSE,
                                  itemPosX[i - State->FirstVisible], row0PosY);
            }  
        } else {
            DrawMainMenuEntry(Screen->Entries[i],
                              (i == State->CurrentSelection) ? TRUE : FALSE,
                              itemPosX[i], row1PosY);
        }
    }
    if (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_LABEL) && (!PointerActive || (PointerActive && DrawSelection))) {
        DrawTextWithTransparency(L"", 0, textPosY);
        DrawTextWithTransparency(Screen->Entries[State->CurrentSelection]->Title,
                                 (UGAWidth - egComputeTextWidth(Screen->Entries[State->CurrentSelection]->Title)) >> 1,
                                 textPosY);
    } else {
          DrawTextWithTransparency(L"", 0, textPosY);
    }

    if (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_HINTS)) {
        DrawTextWithTransparency(Screen->Hint1, (UGAWidth - egComputeTextWidth(Screen->Hint1)) / 2,
                                 UGAHeight - (egGetFontHeight() * 3));
        DrawTextWithTransparency(Screen->Hint2, (UGAWidth - egComputeTextWidth(Screen->Hint2)) / 2,
                                 UGAHeight - (egGetFontHeight() * 2));
    }  
}  

static VOID PaintSelection(IN REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, UINTN *itemPosX,
                           UINTN row0PosY, UINTN row1PosY, UINTN textPosY) {
    UINTN XSelectPrev, XSelectCur, YPosPrev, YPosCur;

    if (((State->CurrentSelection <= State->LastVisible) &&
        (State->CurrentSelection >= State->FirstVisible)) ||
        (State->CurrentSelection >= State->InitialRow1) ) {
        if (Screen->Entries[State->PreviousSelection]->Row == 0) {
            XSelectPrev = State->PreviousSelection - State->FirstVisible;
            YPosPrev = row0PosY;
        } else {
            XSelectPrev = State->PreviousSelection;
            YPosPrev = row1PosY;
        }  
        if (Screen->Entries[State->CurrentSelection]->Row == 0) {
            XSelectCur = State->CurrentSelection - State->FirstVisible;
            YPosCur = row0PosY;
        } else {
            XSelectCur = State->CurrentSelection;
            YPosCur = row1PosY;
        }  
        DrawMainMenuEntry(Screen->Entries[State->PreviousSelection], FALSE,
                          itemPosX[XSelectPrev], YPosPrev);
        DrawMainMenuEntry(Screen->Entries[State->CurrentSelection], TRUE,
                          itemPosX[XSelectCur], YPosCur);
        if (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_LABEL) && (!PointerActive || (PointerActive && DrawSelection))) {
            DrawTextWithTransparency(L"", 0, textPosY);
            DrawTextWithTransparency(Screen->Entries[State->CurrentSelection]->Title,
                                     (UGAWidth - egComputeTextWidth(Screen->Entries[State->CurrentSelection]->Title)) >> 1,
                                     textPosY);
        } else {
             DrawTextWithTransparency(L"", 0, textPosY);
        }
    } else {  
        MainMenuStyle(Screen, State, MENU_FUNCTION_PAINT_ALL, NULL);
    }
}  

static EG_IMAGE * GetIcon(IN EG_EMBEDDED_IMAGE *BuiltInIcon, IN CHAR16 *ExternalFilename){
    EG_IMAGE * Icon = egFindIcon(ExternalFilename, GlobalConfig.IconSizes[ICON_SIZE_SMALL]);
    if(Icon != NULL) return Icon;
    return egPrepareEmbeddedImage(BuiltInIcon, TRUE);
}

UINTN ComputeRow0PosY(VOID) {
    return ((UGAHeight / 2) - TileSizes[0] / 2);
}  

static VOID ClearWithBackground(UINTN PosX, UINTN PosY, UINTN Width, UINTN Height){
    EG_IMAGE * TempImage = egCropImage(GlobalConfig.ScreenBackground, PosX, PosY, Width, Height);
    BltImage(TempImage, PosX, PosY);
    egFreeImage(TempImage);
}

static VOID PaintArrow(EG_IMAGE * Arrow, UINTN PosX, UINTN PosY, BOOLEAN visible) {
    UINTN TopY = PosY - (Arrow->Height / 2);
    if (visible) egDrawImageWithTransparency(Arrow, NULL, PosX, TopY, Arrow->Width, Arrow->Height);
    else ClearWithBackground(PosX, TopY, Arrow->Width, Arrow->Height);
}

static VOID PaintArrows(SCROLL_STATE *State, UINTN PosX, UINTN PosY, UINTN row0Loaders) {
    BOOLEAN HideFlagArrows = GlobalConfig.HideUIFlags & HIDEUI_FLAG_ARROWS;
    if(!HideFlagArrows){

        EG_IMAGE * LeftArrow = GetIcon(&egemb_arrow_left, L"arrow_left");
        if(LeftArrow){
            UINTN LeftX = PosX - LeftArrow->Width;
            PaintArrow(LeftArrow, LeftX, PosY, State->FirstVisible > 0);
            egFreeImage(LeftArrow);
        }

        EG_IMAGE * RightArrow = GetIcon(&egemb_arrow_right, L"arrow_right");
        if(RightArrow){
            UINTN RightX = (UGAWidth + (TileSizes[0] + TILE_XSPACING) * State->MaxVisible) / 2 + TILE_XSPACING;
            PaintArrow(RightArrow, RightX, PosY, State->LastVisible < (row0Loaders - 1));
            egFreeImage(RightArrow);
        }

    }
}  

/*
 * Places menu entries and tool rows according to runtime screen geometry;
 * rendering an icon is unrelated to the image's trust or bootability.
 */
VOID MainMenuStyle(IN REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State,
                   IN UINTN Function, IN CHAR16 *ParamText)
{
    INTN i;
    static UINTN row0PosX, row0PosXRunning, row1PosY, row0Loaders;
    UINTN row0Count, row1Count, row1PosX, row1PosXRunning;
    static UINTN *itemPosX;
    static UINTN row0PosY, textPosY;

    State->ScrollMode = SCROLL_MODE_ICONS;
    switch (Function) {

        case MENU_FUNCTION_INIT:
            InitScroll(State, Screen->EntryCount, GlobalConfig.MaxTags);

            row0Count = 0;
            row1Count = 0;
            row0Loaders = 0;
            for (i = 0; i <= State->MaxIndex; i++) {
               if (Screen->Entries[i]->Row == 1) {
                  row1Count++;
               } else {
                  row0Loaders++;
                  if (row0Count < State->MaxVisible)
                     row0Count++;
               }
            }
            row0PosX = (UGAWidth + TILE_XSPACING - (TileSizes[0] + TILE_XSPACING) * row0Count) >> 1;
            row0PosY = ComputeRow0PosY();
            row1PosX = (UGAWidth + TILE_XSPACING - (TileSizes[1] + TILE_XSPACING) * row1Count) >> 1;
            row1PosY = row0PosY + TileSizes[0] + TILE_YSPACING;
            if (row1Count > 0)
                textPosY = row1PosY + TileSizes[1] + TILE_YSPACING;
            else
                textPosY = row1PosY;

            itemPosX = AllocatePool(sizeof(UINTN) * Screen->EntryCount);
            row0PosXRunning = row0PosX;
            row1PosXRunning = row1PosX;
            for (i = 0; i <= State->MaxIndex; i++) {
                if (Screen->Entries[i]->Row == 0) {
                    itemPosX[i] = row0PosXRunning;
                    row0PosXRunning += TileSizes[0] + TILE_XSPACING;
                } else {
                    itemPosX[i] = row1PosXRunning;
                    row1PosXRunning += TileSizes[1] + TILE_XSPACING;
                }
            }

            InitSelection();
            SwitchToGraphicsAndClear();
            break;

        case MENU_FUNCTION_CLEANUP:
            MyFreePool(itemPosX);
            break;

        case MENU_FUNCTION_PAINT_ALL:
            PaintAll(Screen, State, itemPosX, row0PosY, row1PosY, textPosY);
            PaintArrows(State, row0PosX - TILE_XSPACING, row0PosY + (TileSizes[0] / 2), row0Loaders);
            break;

        case MENU_FUNCTION_PAINT_SELECTION:
            PaintSelection(Screen, State, itemPosX, row0PosY, row1PosY, textPosY);
            break;

        case MENU_FUNCTION_PAINT_TIMEOUT:
            if (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_LABEL)) {
               DrawTextWithTransparency(L"", 0, textPosY + TextLineHeight());
               DrawTextWithTransparency(ParamText, (UGAWidth - egComputeTextWidth(ParamText)) >> 1, textPosY + TextLineHeight());
            }
            break;

    }
}  

/**
 * Map the pointer coordinates to one of the two icon rows. Row zero uses the
 * visible-window index offset, while row one uses absolute entry indices. Areas
 * to the left and right of the loader row return arrow sentinels. Tile gaps
 * return POINTER_NO_ITEM.
 */
UINTN FindMainMenuItem(IN REFIT_MENU_SCREEN *Screen, IN SCROLL_STATE *State, IN UINTN PosX, IN UINTN PosY)
{
    UINTN i;
    static UINTN row0PosX, row0PosXRunning, row1PosY, row0Loaders;
    UINTN row0Count, row1Count, row1PosX, row1PosXRunning;
    static UINTN *itemPosX;
    static UINTN row0PosY;
    UINTN itemRow;

    row0Count = 0;
    row1Count = 0;
    row0Loaders = 0;
    for (i = 0; i <= State->MaxIndex; i++) {
        if (Screen->Entries[i]->Row == 1) {
                row1Count++;
        } else {
                row0Loaders++;
                if (row0Count < State->MaxVisible)
                        row0Count++;
        }
    }
    row0PosX = (UGAWidth + TILE_XSPACING - (TileSizes[0] + TILE_XSPACING) * row0Count) >> 1;
    row0PosY = ComputeRow0PosY();
    row1PosX = (UGAWidth + TILE_XSPACING - (TileSizes[1] + TILE_XSPACING) * row1Count) >> 1;
    row1PosY = row0PosY + TileSizes[0] + TILE_YSPACING;

    if (PosY >= row0PosY && PosY <= row0PosY + TileSizes[0]) {
        itemRow = 0;
        if(PosX <= row0PosX) {
            return POINTER_LEFT_ARROW;
        }
        else if(PosX >= (UGAWidth - row0PosX)) {
            return POINTER_RIGHT_ARROW;
        }
    } else if (PosY >= row1PosY && PosY <= row1PosY + TileSizes[1]) {
        itemRow = 1;
    } else {  
        return POINTER_NO_ITEM;
    }

    UINTN ItemIndex = POINTER_NO_ITEM;

    itemPosX = AllocatePool(sizeof(UINTN) * Screen->EntryCount);
    row0PosXRunning = row0PosX;
    row1PosXRunning = row1PosX;
    for (i = 0; i <= State->MaxIndex; i++) {
        if (Screen->Entries[i]->Row == 0) {
            itemPosX[i] = row0PosXRunning;
            row0PosXRunning += TileSizes[0] + TILE_XSPACING;
        } else {
            itemPosX[i] = row1PosXRunning;
            row1PosXRunning += TileSizes[1] + TILE_XSPACING;
        }
    }

    for (i = State->FirstVisible; i <= State->MaxIndex; i++) {
        if (Screen->Entries[i]->Row == 0 && itemRow == 0) {
            if (i <= State->LastVisible) {
                if(PosX >= itemPosX[i - State->FirstVisible] && PosX <= itemPosX[i - State->FirstVisible] + TileSizes[0]) {
                    ItemIndex = i;
                    break;
                }
        }  
        } else if (Screen->Entries[i]->Row == 1 && itemRow == 1) {
            if(PosX >= itemPosX[i] && PosX <= itemPosX[i] + TileSizes[1]) {
                ItemIndex = i;
                break;
            }
        }
    }

    MyFreePool(itemPosX);

    return ItemIndex;
}  

/**
 * Reserve one slot for the keyboard, one per pointer device, and a final slot for
 * a relative timer. The timer handle is created by each timed wait. A call with
 * no deadline passes one fewer slot to firmware.
 */
VOID GenerateWaitList() {
    UINTN PointerCount = pdCount();
    WaitListLength = 2 + PointerCount;

    WaitList = AllocatePool(sizeof(EFI_EVENT) * WaitListLength);

    WaitList[0] = ST->ConIn->WaitForKey;

    UINTN Index;
    for(Index = 0; Index < PointerCount; Index++) {
        WaitList[Index + 1] = pdWaitEvent(Index);
    }
}  

/**
 * Convert the millisecond deadline to firmware intervals of 100 ns. Slot zero
 * identifies the keyboard. With a timer the last active slot means timeout.
 * Without a timer Length is decremented, but the same final-slot test is used, so
 * the last pointer event can also be reported as a timeout.
 */
UINTN WaitForInput(UINTN Timeout) {
    UINTN Index = INPUT_TIMEOUT;
    UINTN Length = WaitListLength;
    EFI_EVENT TimerEvent;
    EFI_STATUS Status;

    LOG(3, LOG_LINE_NORMAL, L"Entering WaitForInput(), Timeout = %d", Timeout);
    Status = refit_call5_wrapper(BS->CreateEvent, EVT_TIMER, 0, NULL, NULL, &TimerEvent);
    /*
     * The wait list starts with the keyboard, then pointers, then an optional
     * timer. A zero timeout excludes its final slot. In that case the last
     * included event is a pointer when one exists, yet the final index test
     * below still classifies that slot as a timeout.
     */
    if (Timeout == 0) {
        Length--;
    } else {
        if(EFI_ERROR(Status)) {
            refit_call1_wrapper(BS->Stall, 100000);  
            return INPUT_TIMER_ERROR;
        } else {
            Status = refit_call3_wrapper(BS->SetTimer, TimerEvent, TimerRelative, Timeout * 10000);
            WaitList[Length - 1] = TimerEvent;
        }
    }

    Status = refit_call3_wrapper(BS->WaitForEvent, Length, WaitList, &Index);
    refit_call1_wrapper(BS->CloseEvent, TimerEvent);

    if(EFI_ERROR(Status)) {
        refit_call1_wrapper(BS->Stall, 100000);  
        return INPUT_TIMER_ERROR;
    } else if (Index == 0) {
        return INPUT_KEY;
    } else if (Index < Length - 1) {
        return INPUT_POINTER;
    }
    return INPUT_TIMEOUT;
}  

static BOOLEAN EditOptions(LOADER_ENTRY *MenuEntry) {
    UINTN x_max, y_max;
    CHAR16 *EditedOptions;
    BOOLEAN retval = FALSE;

    if (GlobalConfig.HideUIFlags & HIDEUI_FLAG_EDITOR) {
        return FALSE;
    }

    refit_call4_wrapper(ST->ConOut->QueryMode, ST->ConOut, ST->ConOut->Mode->Mode, &x_max, &y_max);

    if (!GlobalConfig.TextOnly)
        SwitchToText(TRUE);

    if (line_edit(MenuEntry->LoadOptions, &EditedOptions, x_max)) {
        MyFreePool(MenuEntry->LoadOptions);
        MenuEntry->LoadOptions = EditedOptions;
        retval = TRUE;
    }  
    if (!GlobalConfig.TextOnly)
        SwitchToGraphics();
    return retval;
}  

VOID DisplaySimpleMessage(CHAR16* Title, CHAR16 *Message) {
    MENU_STYLE_FUNC     Style = TextMenuStyle;
    INTN                DefaultEntry = 0;
    REFIT_MENU_ENTRY    *ChosenOption;
    REFIT_MENU_SCREEN   HideItemMenu = { NULL, NULL, 0, NULL, 0, NULL, 0, NULL,
                                         L"Press Enter to return to main menu", L"" };

    LOG(3, LOG_LINE_NORMAL, L"Entering DisplaySimpleMessage()");
    if (!Message)
        return;

    if (AllowGraphicsMode)
        Style = GraphicsMenuStyle;
    HideItemMenu.TitleImage = BuiltinIcon(BUILTIN_ICON_FUNC_ABOUT);
    HideItemMenu.Title = Title;
    AddMenuInfoLine(&HideItemMenu, Message);
    AddMenuEntry(&HideItemMenu, &MenuEntryReturn);
    RunGenericMenu(&HideItemMenu, Style, &DefaultEntry, &ChosenOption);
    LOG(1, LOG_LINE_NORMAL, L"%s - %s", Title, Message);
}  

static BOOLEAN RemoveInvalidFilenames(CHAR16 *FilenameList, CHAR16 *VarName) {
    UINTN i = 0;
    CHAR16 *Filename, *OneElement, *VolName = NULL;
    REFIT_VOLUME *Volume;
    EFI_FILE_HANDLE FileHandle;
    BOOLEAN DeleteIt = FALSE, DeletedSomething = FALSE;
    EFI_STATUS Status;

    while ((OneElement = FindCommaDelimited(FilenameList, i)) != NULL) {
        DeleteIt = FALSE;
        Filename = StrDuplicate(OneElement);
        if (SplitVolumeAndFilename(&Filename, &VolName)) {
            DeleteIt = TRUE;
            if (FindVolume(&Volume, VolName) && Volume->RootDir) {
                Status = refit_call5_wrapper(Volume->RootDir->Open, Volume->RootDir, &FileHandle,
                                             Filename, EFI_FILE_MODE_READ, 0);
                if (Status == EFI_SUCCESS) {
                    DeleteIt = FALSE;
                    refit_call1_wrapper(FileHandle->Close, FileHandle);
                }  
            }  
        }  
        if (DeleteIt) {
            DeleteItemFromCsvList(OneElement, FilenameList);
        } else {
            i++;
        }
        MyFreePool(OneElement);
        MyFreePool(Filename);
        MyFreePool(VolName);
        VolName = NULL;
        DeletedSomething |= DeleteIt;
    }  
    return DeletedSomething;
}  

/*
 * Saves the menu's hidden-entry list. This path changes BOOT UI state,
 * not the underlying EFI executables or competing boot-manager installations.
 */
static VOID SaveHiddenList(IN CHAR16 *HiddenList, IN CHAR16 *VarName) {
    EFI_STATUS Status;
    UINTN i;

    i = HiddenList ? StrLen(HiddenList) : 0;
    Status = EfivarSetRaw(&BootGuid, VarName, (CHAR8 *) HiddenList, i * 2 + 2 * (i > 0), TRUE);
    CheckError(Status, L"in SaveHiddenList()");
}  

/*
 * Presents and edits stored menu-visibility exclusions. Hiding an entry
 * affects discovery and display, not the executable bytes, filesystem,
 * Secure Boot policy or firmware BootOrder records.
 */
VOID ManageHiddenTags(VOID) {
    CHAR16              *AllTags = NULL, *HiddenTags, *HiddenTools;
    CHAR16              *HiddenLegacy, *HiddenFirmware, *OneElement = NULL;
    INTN                DefaultEntry = 0;
    MENU_STYLE_FUNC     Style = TextMenuStyle;
    REFIT_MENU_ENTRY    *ChosenOption, *MenuEntryItem = NULL;
    REFIT_MENU_SCREEN   HideItemMenu = { L"Manage Hidden Tags Menu", NULL, 0, NULL, 0, NULL, 0, NULL,
                                         L"Select an option and press Enter or",
                                         L"press Esc to return to main menu without changes" };
    UINTN               MenuExit, i = 0;
    BOOLEAN             SaveTags, SaveTools, SaveLegacy = FALSE, SaveFirmware = FALSE;

    LOG(1, LOG_LINE_SEPARATOR, L"Managing hidden tags");
    HideItemMenu.TitleImage = BuiltinIcon(BUILTIN_ICON_FUNC_HIDDEN);
    if (AllowGraphicsMode)
        Style = GraphicsMenuStyle;

    HiddenTags = ReadHiddenTags(L"HiddenTags");
    SaveTags = RemoveInvalidFilenames(HiddenTags, L"HiddenTags");
    if (HiddenTags && (HiddenTags[0] != L'\0'))
        AllTags = StrDuplicate(HiddenTags);
    HiddenTools = ReadHiddenTags(L"HiddenTools");
    SaveTools = RemoveInvalidFilenames(HiddenTools, L"HiddenTools");
    if (HiddenTools && (HiddenTools[0] != L'\0'))
        MergeStrings(&AllTags, HiddenTools, L',');
    HiddenLegacy = ReadHiddenTags(L"HiddenLegacy");
    if (HiddenLegacy && (HiddenLegacy[0] != L'\0'))
        MergeStrings(&AllTags, HiddenLegacy, L',');
    HiddenFirmware = ReadHiddenTags(L"HiddenFirmware");
    if (HiddenFirmware && (HiddenFirmware[0] != L'\0'))
        MergeStrings(&AllTags, HiddenFirmware, L',');
    if ((AllTags) && (StrLen(AllTags) > 0)) {
        AddMenuInfoLine(&HideItemMenu, L"Select a tag and press Enter to restore it");
        while ((OneElement = FindCommaDelimited(AllTags, i++)) != NULL) {
            MenuEntryItem = AllocateZeroPool(sizeof(REFIT_MENU_ENTRY));  
            MenuEntryItem->Title = StrDuplicate(OneElement);
            MenuEntryItem->Tag = TAG_RETURN;
            MenuEntryItem->Row = 1;
            AddMenuEntry(&HideItemMenu, MenuEntryItem);
            MyFreePool(OneElement);
        }  
        MenuExit = RunGenericMenu(&HideItemMenu, Style, &DefaultEntry, &ChosenOption);
        if (MenuExit == MENU_EXIT_ENTER) {
            SaveTags |= DeleteItemFromCsvList(ChosenOption->Title, HiddenTags);
            SaveTools |= DeleteItemFromCsvList(ChosenOption->Title, HiddenTools);
            SaveFirmware |= DeleteItemFromCsvList(ChosenOption->Title, HiddenFirmware);
            SaveLegacy |= DeleteItemFromCsvList(ChosenOption->Title, HiddenLegacy);
        }  
        if (SaveTags)
            SaveHiddenList(HiddenTags, L"HiddenTags");
        if (SaveLegacy)
            SaveHiddenList(HiddenLegacy, L"HiddenLegacy");
        if (SaveTools) {
            SaveHiddenList(HiddenTools, L"HiddenTools");
            MyFreePool(gHiddenTools);
            gHiddenTools = NULL;
        }
        if (SaveFirmware)
            SaveHiddenList(HiddenFirmware, L"HiddenFirmware");
        if (SaveTags || SaveTools || SaveLegacy || SaveFirmware)
            RescanAll(FALSE, FALSE);
    } else {
        DisplaySimpleMessage(L"Information", L"No hidden tags found");
    }
    MyFreePool(AllTags);
    MyFreePool(HiddenTags);
    MyFreePool(HiddenTools);
    MyFreePool(HiddenLegacy);
    MyFreePool(HiddenFirmware);
}  

CHAR16* ReadHiddenTags(CHAR16 *VarName) {
    CHAR8       *Buffer = NULL;
    UINTN       Size;
    EFI_STATUS  Status;

    Status = EfivarGetRaw(&BootGuid, VarName, &Buffer, &Size);
    if ((Status != EFI_SUCCESS) && (Status != EFI_NOT_FOUND))
        CheckError(Status, L"in ReadHiddenTags()");
    if ((Status == EFI_SUCCESS) && (Size == 0)) {
        MyFreePool(Buffer);
        Buffer = NULL;
    }
    return (CHAR16 *) Buffer;
}  

static VOID AddToHiddenTags(CHAR16 *VarName, CHAR16 *Pathname) {
    CHAR16      *HiddenTags;
    EFI_STATUS  Status;

    if (Pathname && (StrLen(Pathname) > 0)) {
        HiddenTags = ReadHiddenTags(VarName);
        MergeStrings(&HiddenTags, Pathname, L',');
        Status = EfivarSetRaw(&BootGuid, VarName, (CHAR8 *) HiddenTags, StrLen(HiddenTags) * 2 + 2, TRUE);
        CheckError(Status, L"in AddToHiddenTags()");
        MyFreePool(HiddenTags);
    }  
}  

/*
 * Persists a BOOT-level exclusion; the EFI executable and any independent
 * firmware boot entry remain on disk and usable outside this UI.
 */
static BOOLEAN HideEfiTag(LOADER_ENTRY *Loader, REFIT_MENU_SCREEN *HideItemMenu, CHAR16 *VarName) {
    REFIT_VOLUME       *TestVolume = NULL;
    BOOLEAN            TagHidden = FALSE;
    CHAR16             *FullPath = NULL, *GuidStr = NULL;
    MENU_STYLE_FUNC    Style = TextMenuStyle;
    UINTN              MenuExit;
    INTN               DefaultEntry = 1;
    REFIT_MENU_ENTRY   *ChosenOption;

    if ((!Loader) || (!(Loader->Volume)) || (!(Loader->LoaderPath)) || (!HideItemMenu) || (!VarName))
        return FALSE;

    if (AllowGraphicsMode)
        Style = GraphicsMenuStyle;

    if (Loader->Volume->VolName && (StrLen(Loader->Volume->VolName) > 0)) {
        FullPath = StrDuplicate(Loader->Volume->VolName);
    }
    MergeStrings(&FullPath, Loader->LoaderPath, L':');
    AddMenuInfoLine(HideItemMenu, PoolPrint(L"Really hide %s?", FullPath));
    AddMenuEntry(HideItemMenu, &MenuEntryYes);
    AddMenuEntry(HideItemMenu, &MenuEntryNo);
    MenuExit = RunGenericMenu(HideItemMenu, Style, &DefaultEntry, &ChosenOption);

    if (ChosenOption && MyStriCmp(ChosenOption->Title, L"Yes") && (MenuExit == MENU_EXIT_ENTER)) {
        GuidStr = GuidAsString(&Loader->Volume->PartGuid);
        if (FindVolume(&TestVolume, GuidStr) && TestVolume->RootDir) {
            MyFreePool(FullPath);
            FullPath = NULL;
            MergeStrings(&FullPath, GuidAsString(&Loader->Volume->PartGuid), L'\0');
            MergeStrings(&FullPath, L":", L'\0');
            MergeStrings(&FullPath, Loader->LoaderPath, (Loader->LoaderPath[0] == L'\\' ? L'\0' : L'\\'));
        }
        AddToHiddenTags(VarName, FullPath);
        TagHidden = TRUE;
        MyFreePool(GuidStr);
    }  

    MyFreePool(FullPath);

    return TagHidden;
}  

static BOOLEAN HideFirmwareTag(LOADER_ENTRY *Loader, REFIT_MENU_SCREEN *HideItemMenu) {
    MENU_STYLE_FUNC    Style = TextMenuStyle;
    REFIT_MENU_ENTRY   *ChosenOption;
    INTN               DefaultEntry = 1;
    UINTN              MenuExit;
    BOOLEAN            TagHidden = FALSE;

    if (AllowGraphicsMode)
        Style = GraphicsMenuStyle;

    AddMenuInfoLine(HideItemMenu, PoolPrint(L"Really hide '%s'?", Loader->Title));
    AddMenuEntry(HideItemMenu, &MenuEntryYes);
    AddMenuEntry(HideItemMenu, &MenuEntryNo);
    MenuExit = RunGenericMenu(HideItemMenu, Style, &DefaultEntry, &ChosenOption);
    if (MyStriCmp(ChosenOption->Title, L"Yes") && (MenuExit == MENU_EXIT_ENTER)) {
        AddToHiddenTags(L"HiddenFirmware", Loader->Title);
        TagHidden = TRUE;
    }  
    return TagHidden;
}  

static BOOLEAN HideLegacyTag(LEGACY_ENTRY *LegacyLoader, REFIT_MENU_SCREEN *HideItemMenu) {
    MENU_STYLE_FUNC    Style = TextMenuStyle;
    REFIT_MENU_ENTRY   *ChosenOption;
    INTN               DefaultEntry = 1;
    UINTN              MenuExit;
    CHAR16             *Name = NULL;
    BOOLEAN            TagHidden = FALSE;

    if (AllowGraphicsMode)
        Style = GraphicsMenuStyle;

    if ((GlobalConfig.LegacyType == LEGACY_TYPE_MAC) && LegacyLoader->me.Title)
        Name = StrDuplicate(LegacyLoader->me.Title);
    if ((GlobalConfig.LegacyType == LEGACY_TYPE_UEFI) && LegacyLoader->BdsOption && LegacyLoader->BdsOption->Description)
        Name = StrDuplicate(LegacyLoader->BdsOption->Description);
    if (!Name)
        Name = StrDuplicate(L"Legacy OS");
    AddMenuInfoLine(HideItemMenu, PoolPrint(L"Really hide '%s'?", Name));
    AddMenuEntry(HideItemMenu, &MenuEntryYes);
    AddMenuEntry(HideItemMenu, &MenuEntryNo);
    MenuExit = RunGenericMenu(HideItemMenu, Style, &DefaultEntry, &ChosenOption);
    if (MyStriCmp(ChosenOption->Title, L"Yes") && (MenuExit == MENU_EXIT_ENTER)) {
        AddToHiddenTags(L"HiddenLegacy", Name);
        TagHidden = TRUE;
    }  
    MyFreePool(Name);
    return TagHidden;
}  

static VOID HideTag(REFIT_MENU_ENTRY *ChosenEntry) {
    LOADER_ENTRY       *Loader = (LOADER_ENTRY *) ChosenEntry;
    LEGACY_ENTRY       *LegacyLoader = (LEGACY_ENTRY *) ChosenEntry;
    REFIT_MENU_SCREEN  HideItemMenu = { NULL, NULL, 0, NULL, 0, NULL, 0, NULL,
                                        L"Select an option and press Enter or",
                                        L"press Esc to return to main menu without changes" };

    if (ChosenEntry == NULL)
        return;

    HideItemMenu.TitleImage = BuiltinIcon(BUILTIN_ICON_FUNC_HIDDEN);

    switch (ChosenEntry->Tag) {
        case TAG_LOADER:
            if (Loader->DiscoveryType == DISCOVERY_TYPE_AUTO) {
                HideItemMenu.Title = L"Hide EFI OS Tag";
                HideEfiTag(Loader, &HideItemMenu, L"HiddenTags");
                RescanAll(FALSE, FALSE);
            } else {
                DisplaySimpleMessage(L"Cannot Hide Entry for Manual Boot Stanza",
                                     L"You must edit boot.conf to remove this entry.");
            }
            break;
        case TAG_LEGACY:
        case TAG_LEGACY_UEFI:
            HideItemMenu.Title = L"Hide Legacy OS Tag";
            if (HideLegacyTag(LegacyLoader, &HideItemMenu))
                RescanAll(FALSE, FALSE);
            break;
        case TAG_FIRMWARE_LOADER:
            HideItemMenu.Title = L"Hide Firmware Boot Option Tag";
            if (HideFirmwareTag(Loader, &HideItemMenu))
                RescanAll(FALSE, FALSE);
            break;
        case TAG_ABOUT:
        case TAG_REBOOT:
        case TAG_SHUTDOWN:
        case TAG_EXIT:
        case TAG_FIRMWARE:
        case TAG_CSR_ROTATE:
        case TAG_HIDDEN:
            DisplaySimpleMessage(L"Unable to Comply",
                                 L"To hide an internal tool, edit the 'showtools' line in boot.conf");
            break;
        case TAG_TOOL:
            HideItemMenu.Title = L"Hide Tool Tag";
            HideEfiTag(Loader, &HideItemMenu, L"HiddenTools");
            MyFreePool(gHiddenTools);
            gHiddenTools = NULL;
            RescanAll(FALSE, FALSE);
            break;
    }  
}  

UINTN RunMenu(IN REFIT_MENU_SCREEN *Screen, OUT REFIT_MENU_ENTRY **ChosenEntry)
{
    INTN            DefaultEntry = -1;
    MENU_STYLE_FUNC Style = TextMenuStyle;

    LOG(2, LOG_LINE_NORMAL, L"Entering RunMenu()");
    if (AllowGraphicsMode)
        Style = GraphicsMenuStyle;

    return RunGenericMenu(Screen, Style, &DefaultEntry, ChosenEntry);
}

/**
 * Run the main screen and its optional subscreen using separate selection
 * indices. Disable the initial timeout after the first return. Escaping a
 * subscreen resumes its parent. The returned entry may be a child, while the
 * saved selection title identifies the original top-level choice.
 */
UINTN RunMainMenu(REFIT_MENU_SCREEN *Screen, CHAR16** DefaultSelection, REFIT_MENU_ENTRY **ChosenEntry)
{
    MENU_STYLE_FUNC Style = TextMenuStyle;
    MENU_STYLE_FUNC MainStyle = TextMenuStyle;
    REFIT_MENU_ENTRY *TempChosenEntry;
    CHAR16 *MenuTitle;
    UINTN MenuExit = 0;
    INTN DefaultEntryIndex = -1;
    INTN DefaultSubmenuIndex = -1;

    LOG(2, LOG_LINE_NORMAL, L"Entering RunMainMenu()");
    TileSizes[0] = (GlobalConfig.IconSizes[ICON_SIZE_BIG] * 9) / 8;
    TileSizes[1] = (GlobalConfig.IconSizes[ICON_SIZE_SMALL] * 4) / 3;

    if ((DefaultSelection != NULL) && (*DefaultSelection != NULL)) {

        DefaultEntryIndex = FindMenuShortcutEntry(Screen, *DefaultSelection);
    }

    if (AllowGraphicsMode) {
        Style = GraphicsMenuStyle;
        MainStyle = MainMenuStyle;

        PointerEnabled = PointerActive = pdAvailable();
        DrawSelection = !PointerEnabled;
    }

    GenerateWaitList();

    while (!MenuExit) {
        MenuExit = RunGenericMenu(Screen, MainStyle, &DefaultEntryIndex, &TempChosenEntry);
        Screen->TimeoutSeconds = 0;

        MenuTitle = StrDuplicate(TempChosenEntry->Title);
        if (MenuExit == MENU_EXIT_DETAILS) {
            if (TempChosenEntry->SubScreen != NULL) {
               LOG(3, LOG_LINE_NORMAL, L"About to call RunGenericMenu() on subscreen '%s'", MenuTitle);
               MenuExit = RunGenericMenu(TempChosenEntry->SubScreen,
                                         Style,
                                         &DefaultSubmenuIndex,
                                         &TempChosenEntry);
               LOG(3, LOG_LINE_NORMAL, L"RunGenericMenu() has returned %d", MenuExit);
               if (MenuExit == MENU_EXIT_ESCAPE || TempChosenEntry->Tag == TAG_RETURN)
                   MenuExit = 0;
               if (MenuExit == MENU_EXIT_DETAILS) {
                  if (!EditOptions((LOADER_ENTRY *) TempChosenEntry))
                     MenuExit = 0;
               }  
            } else {  
               MenuExit = 0;
            }
        }  
        if (MenuExit == MENU_EXIT_HIDE) {
            if (GlobalConfig.HiddenTags)
                HideTag(TempChosenEntry);
            MenuExit = 0;
        }  
    }

    if (ChosenEntry)
        *ChosenEntry = TempChosenEntry;
    if (DefaultSelection) {
       MyFreePool(*DefaultSelection);
       *DefaultSelection = MenuTitle;
    }  
    return MenuExit;
}  
