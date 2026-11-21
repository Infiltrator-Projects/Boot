/*
 * Interactive firmware console editing of a boot-option line.
 * The input is a mutable UTF-16 string; cursor offset, viewport origin and
 * allocated output size must remain consistent under every keystroke.
 */
 
 

 

























#include "global.h"
#include "screen.h"
#include "lib.h"
#include "../include/refit_call_wrapper.h"

/*
 * Moves the logical cursor or shifts the visible viewport when the cursor
 * already occupies the left edge. Both indices refer to the same editable
 * UTF-16 line and must be advanced consistently.
 */
static void cursor_left(UINTN *cursor, UINTN *first)
{
    if ((*cursor) > 0)
        (*cursor)--;
    else if ((*first) > 0)
        (*first)--;
}

/*
 * Advances within visible columns while preserving the text-line boundary;
 * once the viewport is full, scrolling its origin replaces cursor movement.
 */
static void cursor_right(UINTN *cursor, UINTN *first, UINTN x_max, UINTN len)
{
    if ((*cursor)+2 < x_max)
        (*cursor)++;
    else if ((*first) + (*cursor) < len)
        (*first)++;
}

/*
 * Edit a private UTF-16 copy. first is the index of the leftmost visible
 * character and cursor is a screen-column offset, so insertions and deletions
 * use first + cursor. Enter transfers the buffer to line_out. Cancelling
 * releases the private copy.
 */
BOOLEAN line_edit(CHAR16 *line_in, CHAR16 **line_out, UINTN x_max) {
    CHAR16 *line;
    UINTN size;
    UINTN len;
    UINTN first;
    UINTN y_pos = 3;
    CHAR16 *print;
    UINTN cursor;
    BOOLEAN exit;
    BOOLEAN enter;

    DrawScreenHeader(L"Line Editor");
    refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, (ConWidth - 71) / 2, ConHeight - 1);
    refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut,
                        L"Use cursor keys to edit, Esc to exit, Enter to boot with edited options");

    if (!line_in)
        line_in = L"";
    size = StrLen(line_in) + 1024;
    line = AllocatePool(size * sizeof(CHAR16));
    StrCpy(line, line_in);
    len = StrLen(line);
    print = AllocatePool(x_max * sizeof(CHAR16));

    refit_call2_wrapper(ST->ConOut->EnableCursor, ST->ConOut, TRUE);

    first = 0;
    cursor = 0;
    enter = FALSE;
    exit = FALSE;
    while (!exit) {
        UINTN index;
        EFI_STATUS err;
        EFI_INPUT_KEY key;
        UINTN i;

        i = len - first;
        if (i >= x_max-2)
            i = x_max-2;
        MyCopyMem(print, line + first, i * sizeof(CHAR16));
        print[i++] = ' ';
        print[i] = '\0';

        refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, 0, y_pos);
        refit_call2_wrapper(ST->ConOut->OutputString, ST->ConOut, print);
        refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, cursor, y_pos);

        refit_call3_wrapper(BS->WaitForEvent, 1, &ST->ConIn->WaitForKey, &index);
        err = refit_call2_wrapper(ST->ConIn->ReadKeyStroke, ST->ConIn, &key);
        if (EFI_ERROR(err))
            continue;

        switch (key.ScanCode) {
        case SCAN_ESC:
            exit = TRUE;
            break;
        case SCAN_HOME:
            cursor = 0;
            first = 0;
            continue;
        case SCAN_END:
            cursor = len;
            if (cursor >= x_max) {
                cursor = x_max-2;
                first = len - (x_max-2);
            }
            continue;
        case SCAN_UP:
            while((first + cursor) && line[first + cursor] == ' ')
                cursor_left(&cursor, &first);
            while((first + cursor) && line[first + cursor] != ' ')
                cursor_left(&cursor, &first);
            while((first + cursor) && line[first + cursor] == ' ')
                cursor_left(&cursor, &first);
            if (first + cursor != len && first + cursor)
                cursor_right(&cursor, &first, x_max, len);
            refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, cursor, y_pos);
            continue;
        case SCAN_DOWN:
            while(line[first + cursor] && line[first + cursor] == ' ')
                cursor_right(&cursor, &first, x_max, len);
            while(line[first + cursor] && line[first + cursor] != ' ')
                cursor_right(&cursor, &first, x_max, len);
            while(line[first + cursor] && line[first + cursor] == ' ')
                cursor_right(&cursor, &first, x_max, len);
            refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, cursor, y_pos);
            continue;
        case SCAN_RIGHT:
            if (first + cursor == len)
                continue;
            cursor_right(&cursor, &first, x_max, len);
            refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, cursor, y_pos);
            continue;
        case SCAN_LEFT:
            cursor_left(&cursor, &first);
            refit_call3_wrapper(ST->ConOut->SetCursorPosition, ST->ConOut, cursor, y_pos);
            continue;
        case SCAN_DELETE:
            if (len == 0)
                continue;
            if (first + cursor == len)
                continue;
            for (i = first + cursor; i < len; i++)
                line[i] = line[i+1];
            line[len-1] = ' ';
            len--;
            continue;
        }

        switch (key.UnicodeChar) {
        case CHAR_LINEFEED:
        case CHAR_CARRIAGE_RETURN:
            *line_out = line;
            line = NULL;
            enter = TRUE;
            exit = TRUE;
            break;
        case CHAR_BACKSPACE:
            if (len == 0)
                continue;
            if (first == 0 && cursor == 0)
                continue;
            for (i = first + cursor-1; i < len; i++)
                line[i] = line[i+1];
            len--;
            if (cursor > 0)
                cursor--;
            if (cursor > 0 || first == 0)
                continue;
             
            if (len < x_max-2) {
                cursor = first;
                first = 0;
                continue;
            }
             
            if (first > 10) {
                first -= 10;
                cursor = 10;
            } else {
                cursor = first;
                first = 0;
            }
            continue;
        case '\t':
        case ' ' ... '~':
        case 0x80 ... 0xffff:
            if (len+1 == size)
                continue;
            for (i = len; i > first + cursor; i--)
                line[i] = line[i-1];
            line[first + cursor] = key.UnicodeChar;
            len++;
            line[len] = '\0';
            if (cursor+2 < x_max)
                cursor++;
            else if (first + cursor < len)
                first++;
            continue;
        }
    }

    refit_call2_wrapper(ST->ConOut->EnableCursor, ST->ConOut, FALSE);
    MyFreePool(print);
    MyFreePool(line);
    return enter;
}  
