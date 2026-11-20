/*
 * boot/install.c
 * Functions related to installation of BOOT and management of EFI boot order
 *
 * Copyright (c) 2020-2024 by Roderick W. Smith
 *
 * Distributed under the terms of the GNU General Public License (GPL)
 * version 3 (GPLv3), a copy of which must be distributed with this source
 * code or binaries made from it.
 *
 */

#include "global.h"
#include "lib.h"
#include "screen.h"
#include "install.h"
#include "log.h"
#include "menu.h"
#include "mystrings.h"
#include "../include/refit_call_wrapper.h"
#include "../include/Handle.h"

/* Generic EFI file rename used by log rotation; it does not deploy BOOT. */
EFI_STATUS BackupOldFile(IN EFI_FILE_PROTOCOL *BaseDir, CHAR16 *FileName) {
    EFI_FILE_PROTOCOL *FilePtr = NULL;
    EFI_FILE_INFO *Original = NULL, *Replacement = NULL;
    CHAR16 *BackupName = NULL;
    UINTN InformationBytes;
    EFI_STATUS Status;

    if (!BaseDir || !FileName) return EFI_INVALID_PARAMETER;
    BackupName = PoolPrint(L"%s-old", FileName);
    if (!BackupName) return EFI_OUT_OF_RESOURCES;
    if (FileExists(BaseDir, BackupName)) {
        MyFreePool(BackupName);
        return EFI_ALREADY_STARTED;
    }
    Status = refit_call5_wrapper(BaseDir->Open, BaseDir, &FilePtr, FileName,
                                 EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (EFI_ERROR(Status) || !FilePtr) goto finished;
    Original = LibFileInfo(FilePtr);
    if (!Original) {
        Status = EFI_OUT_OF_RESOURCES;
        goto finished;
    }
    InformationBytes = sizeof(EFI_FILE_INFO) + StrSize(BackupName);
    Replacement = AllocateZeroPool(InformationBytes);
    if (!Replacement) {
        Status = EFI_OUT_OF_RESOURCES;
        goto finished;
    }
    MyCopyMem(Replacement, Original, sizeof(EFI_FILE_INFO));
    Replacement->Size = InformationBytes;
    Replacement->FileName[0] = 0;
    StrCat(Replacement->FileName, BackupName);
    Status = refit_call4_wrapper(BaseDir->SetInfo, FilePtr,
                                 &gEfiFileInfoGuid, InformationBytes, Replacement);
finished:
    if (FilePtr) refit_call1_wrapper(FilePtr->Close, FilePtr);
    MyFreePool(Original);
    MyFreePool(Replacement);
    MyFreePool(BackupName);
    return Status;
}

/* Firmware self-installation retired: only the native RUN deploys BOOT.
 * No firmware-side operation may write the shared EFI/BOOT fallback.
 */

static EFI_STATUS SetBootDefault(UINTN BootNum) {
    UINTN Status, VarSize = 0, ListSize, i, j;
    UINT16 *BootOrder = NULL, *NewBootOrder = NULL;
    BOOLEAN AlreadyFirst = FALSE;

    Status = EfivarGetRaw(&GlobalGuid, L"BootOrder", (CHAR8 **)&BootOrder, &VarSize);
    if (EFI_ERROR(Status) || !BootOrder ||
        (VarSize % sizeof(UINT16)) || VarSize == 0) {
        MyFreePool(BootOrder);
        return EFI_COMPROMISED_DATA;
    }
    ListSize = VarSize / sizeof(UINT16);
    AlreadyFirst = (BootOrder[0] == BootNum);
    if (AlreadyFirst) {
        MyFreePool(BootOrder);
        return EFI_SUCCESS;
    }
    if (ListSize >= ((UINTN)~(UINTN)0) / sizeof(UINT16) - 1) {
        MyFreePool(BootOrder);
        return EFI_OUT_OF_RESOURCES;
    }
    NewBootOrder = AllocateZeroPool((ListSize + 1) * sizeof(UINT16));
    if (!NewBootOrder) {
        MyFreePool(BootOrder);
        return EFI_OUT_OF_RESOURCES;
    }
    NewBootOrder[0] = (UINT16)BootNum;
    j = 1;
    for (i = 0; i < ListSize; ++i) {
        if (BootOrder[i] != BootNum)
            NewBootOrder[j++] = BootOrder[i];
    }
    Status = EfivarSetRaw(&GlobalGuid, L"BootOrder", (CHAR8 *)NewBootOrder,
                          j * sizeof(UINT16), TRUE);
    MyFreePool(NewBootOrder);
    MyFreePool(BootOrder);
    return Status;
} // EFI_STATUS SetBootDefault()

/* Validate the complete firmware Boot#### record before accessing any member. */
BOOT_ENTRY_LIST * FindBootOrderEntries(VOID) {
    UINTN Status, VarSize, OrderBytes = 0, i, charCount, pathOffset;
    UINT16 *BootOrder = NULL, pathBytes;
    UINT32 options;
    CHAR16 *VarName, *Label;
    CHAR8 *Contents;
    BOOT_ENTRY_LIST *L, *ListStart = NULL, *ListEnd = NULL;

    Status = EfivarGetRaw(&GlobalGuid, L"BootOrder", (CHAR8 **)&BootOrder, &OrderBytes);
    if (EFI_ERROR(Status) || !BootOrder || (OrderBytes % sizeof(UINT16))) {
        MyFreePool(BootOrder);
        return NULL;
    }
    for (i = 0; i < OrderBytes / sizeof(UINT16); ++i) {
        VarName = PoolPrint(L"Boot%04x", BootOrder[i]);
        if (!VarName) break;
        Contents = NULL;
        VarSize = 0;
        Status = EfivarGetRaw(&GlobalGuid, VarName, &Contents, &VarSize);
        MyFreePool(VarName);
        if (EFI_ERROR(Status) || !Contents || VarSize < 8) {
            MyFreePool(Contents);
            continue;
        }
        MyCopyMem(&options, Contents, sizeof(options));
        MyCopyMem(&pathBytes, Contents + 4, sizeof(pathBytes));
        Label = (CHAR16 *)(VOID *)(Contents + 6);
        for (charCount = 0;
             charCount < (VarSize - 6) / sizeof(CHAR16) &&
             Label[charCount] != 0; ++charCount) {}
        if (charCount == (VarSize - 6) / sizeof(CHAR16)) {
            MyFreePool(Contents);
            continue;
        }
        pathOffset = 6 + (charCount + 1) * sizeof(CHAR16);
        if (pathBytes < sizeof(EFI_DEVICE_PATH) ||
            pathOffset > VarSize || pathBytes > VarSize - pathOffset) {
            MyFreePool(Contents);
            continue;
        }
        L = AllocateZeroPool(sizeof(*L));
        if (!L) { MyFreePool(Contents); break; }
        L->BootEntry.Label = StrDuplicate(Label);
        L->BootEntry.DevPath = AllocatePool(pathBytes);
        if (!L->BootEntry.Label || !L->BootEntry.DevPath) {
            MyFreePool(L->BootEntry.Label);
            MyFreePool(L->BootEntry.DevPath);
            MyFreePool(L);
            MyFreePool(Contents);
            break;
        }
        L->BootEntry.BootNum = BootOrder[i];
        L->BootEntry.Options = options;
        L->BootEntry.Size = pathBytes;
        MyCopyMem(L->BootEntry.DevPath, Contents + pathOffset, pathBytes);
        if (ListEnd) ListEnd->NextBootEntry = L;
        else ListStart = L;
        ListEnd = L;
        MyFreePool(Contents);
    }
    MyFreePool(BootOrder);
    return ListStart;
}

// Delete a linked-list BOOT_ENTRY_LIST data structure
VOID DeleteBootOrderEntries(BOOT_ENTRY_LIST *Entries) {
    BOOT_ENTRY_LIST *Current;

    while (Entries != NULL) {
        Current = Entries;
        MyFreePool(Current->BootEntry.Label);
        MyFreePool(Current->BootEntry.DevPath);
        Entries = Entries->NextBootEntry;
        MyFreePool(Current);
    }
} // VOID DeleteBootOrderEntries()

// Enable the user to pick one boot option to move to the top of the boot
// order list (via Enter) or delete (via Delete or '-'). This function does
// not actually call those options, though; that's left to the calling
// function.
// Returns the operation (EFI_BOOT_OPTION_MAKE_DEFAULT, EFI_BOOT_OPTION_DELETE,
// or EFI_BOOT_OPTION_DO_NOTHING).
// Input variables:
//  - *Entries: Linked-list set of boot entries. Unmodified.
//  - *BootOrderNum: Returns the Boot#### number to be promoted or deleted.
static UINTN PickOneBootOption(IN BOOT_ENTRY_LIST *Entries, IN OUT UINTN *BootOrderNum) {
    CHAR16              *Temp = NULL, *Filename = NULL;
    REFIT_VOLUME        *Volume = NULL;
    INTN                DefaultEntry = 0, MenuExit = MENU_EXIT_ESCAPE;
    UINTN               Operation = EFI_BOOT_OPTION_DO_NOTHING;
    MENU_STYLE_FUNC     Style = TextMenuStyle;
    REFIT_MENU_ENTRY    *ChosenOption, *MenuEntryItem = NULL;
    REFIT_MENU_SCREEN   Menu = { L"Manage EFI Boot Order", NULL, 0, NULL, 0, NULL, 0, NULL,
                                 L"Select an option and press Enter to make it the default, or",
                                 L"press Esc to return without changes" };
    if (AllowGraphicsMode)
        Style = GraphicsMenuStyle;

    if (Entries) {
        AddMenuInfoLine(&Menu, L"Select an option and press Enter to make it the default");
        while (Entries != NULL) {
            MenuEntryItem = AllocateZeroPool(sizeof(REFIT_MENU_ENTRY));
            FindVolumeAndFilename(Entries->BootEntry.DevPath, &Volume, &Filename);
            if ((Filename != NULL) && (StrLen(Filename) > 0)) {
                if ((Volume != NULL) && (Volume->VolName != NULL)) {
                    Temp = PoolPrint(L"Boot%04x - %s - %s on %s",
                                    Entries->BootEntry.BootNum,
                                    Entries->BootEntry.Label,
                                    Filename, Volume->VolName);
                } else {
                    Temp = PoolPrint(L"Boot%04x - %s - %s",
                                    Entries->BootEntry.BootNum,
                                    Entries->BootEntry.Label,
                                    Filename);
                } // if/else
            } else {
                Temp = PoolPrint(L"Boot%04x - %s",
                                 Entries->BootEntry.BootNum,
                                 Entries->BootEntry.Label);
            } // if/else

            MyFreePool(Filename);
            Filename = NULL;
            Volume = NULL;
            MenuEntryItem->Title = StrDuplicate(Temp);
            MenuEntryItem->Row = Entries->BootEntry.BootNum; // Not really the row; the Boot#### number
            AddMenuEntry(&Menu, MenuEntryItem);
            Entries = Entries->NextBootEntry;
            MyFreePool(Temp);
        } // while
        MenuExit = RunGenericMenu(&Menu, Style, &DefaultEntry, &ChosenOption);
        if (MenuExit == MENU_EXIT_ENTER) {
            Operation = EFI_BOOT_OPTION_MAKE_DEFAULT;
            *BootOrderNum = ChosenOption->Row;
        } // if
        /* Firmware entries are never deleted from this interface. */
        MyFreePool(MenuEntryItem);
    } else {
        DisplaySimpleMessage(L"Information", L"EFI boot order list is unavailable");
    } // if
    return Operation;
} // REFIT_VOLUME *PickOneBootOption()



VOID ManageBootorder(VOID) {
    BOOT_ENTRY_LIST *Entries = FindBootOrderEntries();
    UINTN BootNum = 0, Operation;
    EFI_STATUS Status;
    CHAR16 *Message;

    Operation = PickOneBootOption(Entries, &BootNum);
    if (Operation == EFI_BOOT_OPTION_MAKE_DEFAULT) {
        Status = SetBootDefault(BootNum);
        if (EFI_ERROR(Status)) {
            DisplaySimpleMessage(L"Warning", L"Unable to change EFI BootOrder");
        } else {
            Message = PoolPrint(L"Boot%04x is now the default EFI boot option.", BootNum);
            if (Message) {
                DisplaySimpleMessage(L"Information", Message);
                MyFreePool(Message);
            }
        }
    }
    DeleteBootOrderEntries(Entries);
} // VOID ManageBootorder()
