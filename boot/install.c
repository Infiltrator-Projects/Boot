/*
 * Firmware-side EFI boot-option inspection and menu management.
 * Global BootOrder and Boot#### records are external binary input with explicit
 * length and device-path encoding constraints. This code is distinct from
 * the host's boot-native-installer.run installation and ESP write logic.
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

/*
 * Renames an existing EFI file to a sibling '-old' name by SetInfo rather
 * than copying payload bytes. Refuses to overwrite an existing backup and
 * releases the opened EFI file and all temporary pool allocations on exit.
 * EFI firmware filesystem rename semantics determine persistence.
 */
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

/**
 * Promote a 16-bit boot option while preserving the relative order of other
 * entries. Remove later duplicates even if the selected entry was already
 * first. A unique first entry needs no NVRAM rewrite.
 *
 * Preconditions: BootNum fits UINT16; BootOrder is nonempty and consists of
 * complete UINT16 records. Both buffers are owned until the firmware write
 * finishes. A failed SetVariable status propagates to the caller without
 * changing any of the buffers retained by this function.
 * Promoting 7 in [2,7,4,7] produces [7,2,4].
 */
static EFI_STATUS SetBootDefault(UINTN BootNum) {
    EFI_STATUS Status;
    UINTN VarSize = 0, ListSize, i, j, Matches = 0;
    UINT16 *BootOrder = NULL, *NewBootOrder = NULL;

    if (BootNum > 0xffff)
        return EFI_INVALID_PARAMETER;
    Status = EfivarGetRaw(&GlobalGuid, L"BootOrder", (CHAR8 **)&BootOrder, &VarSize);
    if (EFI_ERROR(Status) || !BootOrder ||
        (VarSize % sizeof(UINT16)) || VarSize == 0) {
        MyFreePool(BootOrder);
        return EFI_COMPROMISED_DATA;
    }

    ListSize = VarSize / sizeof(UINT16);
    for (i = 0; i < ListSize; ++i) {
        if (BootOrder[i] == (UINT16)BootNum)
            ++Matches;
    }
    if (BootOrder[0] == (UINT16)BootNum && Matches == 1) {
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
        if (BootOrder[i] != (UINT16)BootNum)
            NewBootOrder[j++] = BootOrder[i];
    }
    Status = EfivarSetRaw(&GlobalGuid, L"BootOrder", (CHAR8 *)NewBootOrder,
                          j * sizeof(UINT16), TRUE);
    MyFreePool(NewBootOrder);
    MyFreePool(BootOrder);
    return Status;
}  

/*
 * Walks device-path nodes with explicit length bounds and requires the
 * terminal end node. A malformed node must never advance into another record.
 */
static BOOLEAN BootDevicePathValid(const CHAR8 *Path, UINTN PathBytes) {
    UINTN Offset = 0;
    UINTN Length;
    UINT8 Type, SubType;

    if (!Path || PathBytes < 4) return FALSE;
    while (Offset + 4 <= PathBytes) {
        Type = (UINT8)Path[Offset];
        SubType = (UINT8)Path[Offset + 1];
        Length = (UINT8)Path[Offset + 2] |
                 ((UINTN)(UINT8)Path[Offset + 3] << 8);
        if (Length < 4 || Length > PathBytes - Offset) return FALSE;
        if (Type == 0x7f) {

            return SubType == 0xff && Length == 4 &&
                   Offset + Length == PathBytes;
        }
        Offset += Length;
    }
    return FALSE;
}

/**
 * For each BootOrder number, unpack the attributes and the 16-bit device-path
 * byte count, then locate the UTF-16 description terminator within the returned
 * record. Validate the path span before copying it. The linked list owns both the
 * label and the path independently of the variable buffer.
 *
 * BootOrder is a sequence of native UINT16 option numbers; an odd byte count
 * rejects the whole sequence. Each Boot#### record begins with a four-byte
 * attributes word and a two-byte FilePathListLength. The UTF-16 description
 * must terminate inside the remaining record; the path length must fit after
 * that terminator and finish with a bounded end-of-device-path node. Bytes
 * after FilePathListLength are optional load data, not part of path validation.
 * A malformed or missing option skips only that number. Allocation failure ends
 * enumeration and returns the valid prefix accumulated so far.
 * DeleteBootOrderEntries releases that list.
 *
 * This is intentionally a partial-result API: NULL may mean no usable
 * entries, malformed BootOrder, or allocation/read failure. A non-NULL list
 * does not prove all Boot#### entries were inspected successfully. Consumers
 * must not interpret a shortened list as authoritative firmware state.
 * BootDevicePathValid checks the bounded node structure, not the semantic
 * validity of a loader or that firmware can launch its target.
 */
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
        /*
         * The fixed prefix ends at byte six. FilePathListLength counts just
         * the device-path bytes following the terminated UTF-16 description.
         * Vendor-specific optional data can follow that list and is outside
         * the path validator's bounds.
         */
        pathOffset = 6 + (charCount + 1) * sizeof(CHAR16);
        if (pathBytes < sizeof(EFI_DEVICE_PATH) ||
            pathOffset > VarSize || pathBytes > VarSize - pathOffset ||
            !BootDevicePathValid(Contents + pathOffset, pathBytes)) {
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

/*
 * Releases the owned label and device-path payload for each parsed Boot####
 * entry, then the list node. Ownership does not extend to firmware NVRAM:
 * this is memory cleanup and does not delete any boot option variable.
 */
VOID DeleteBootOrderEntries(BOOT_ENTRY_LIST *Entries) {
    BOOT_ENTRY_LIST *Current;

    while (Entries != NULL) {
        Current = Entries;
        MyFreePool(Current->BootEntry.Label);
        MyFreePool(Current->BootEntry.DevPath);
        Entries = Entries->NextBootEntry;
        MyFreePool(Current);
    }
}  

/*
 * Present parsed Boot#### records without mutating firmware NVRAM. For each
 * readable record, allocate a temporary menu entry and its descriptive title.
 * Menu.Entries and Menu.InfoLines contain pool-owned pointer arrays; these,
 * each entry and its title must be released only after RunGenericMenu has
 * returned and the chosen numeric Boot#### identifier has been copied out.
 *
 * The return value is an operation code (do nothing, promote or delete);
 * BootOrderNum is updated only for a supported selection. A menu escape,
 * missing list, empty menu or allocation failure can all produce no action.
 * Volume names and filenames are UI descriptions, not verified EFI image
 * identities. The caller owns the provided BOOT_ENTRY_LIST.
 */
static UINTN PickOneBootOption(IN BOOT_ENTRY_LIST *Entries, IN OUT UINTN *BootOrderNum) {
    CHAR16              *Temp = NULL, *Filename = NULL;
    REFIT_VOLUME        *Volume = NULL;
    INTN                DefaultEntry = 0, MenuExit = MENU_EXIT_ESCAPE;
    UINTN               Operation = EFI_BOOT_OPTION_DO_NOTHING;
    UINTN               i, PriorCount;
    MENU_STYLE_FUNC     Style = TextMenuStyle;
    REFIT_MENU_ENTRY    *ChosenOption = NULL, *MenuEntryItem = NULL;
    REFIT_MENU_SCREEN   Menu = { L"Manage EFI Boot Order", NULL, 0, NULL, 0, NULL, 0, NULL,
                                 L"Select an option and press Enter to make it the default, press '-' or",
                                 L"Delete to delete it, or Esc to return to main menu without changes" };

    if (AllowGraphicsMode)
        Style = GraphicsMenuStyle;

    if (Entries) {
        AddMenuInfoLine(&Menu, L"Select an option and press Enter to make it the default or '-' to delete it");
        while (Entries != NULL) {
            MenuEntryItem = AllocateZeroPool(sizeof(*MenuEntryItem));
            if (!MenuEntryItem)
                break;
            FindVolumeAndFilename(Entries->BootEntry.DevPath, &Volume, &Filename);
            if ((Filename != NULL) && (StrLen(Filename) > 0)) {
                if ((Volume != NULL) && (Volume->VolName != NULL))
                    Temp = PoolPrint(L"Boot%04x - %s - %s on %s",
                                     Entries->BootEntry.BootNum, Entries->BootEntry.Label,
                                     Filename, Volume->VolName);
                else
                    Temp = PoolPrint(L"Boot%04x - %s - %s",
                                     Entries->BootEntry.BootNum, Entries->BootEntry.Label, Filename);
            } else {
                Temp = PoolPrint(L"Boot%04x - %s",
                                 Entries->BootEntry.BootNum, Entries->BootEntry.Label);
            }
            MyFreePool(Filename);
            Filename = NULL;
            Volume = NULL;
            if (!Temp) {
                MyFreePool(MenuEntryItem);
                break;
            }
            MenuEntryItem->Title = Temp;
            Temp = NULL;
            MenuEntryItem->Row = Entries->BootEntry.BootNum;
            PriorCount = Menu.EntryCount;
            AddMenuEntry(&Menu, MenuEntryItem);
            if (Menu.EntryCount == PriorCount) {
                MyFreePool(MenuEntryItem->Title);
                MyFreePool(MenuEntryItem);
                break;
            }
            Entries = Entries->NextBootEntry;
        }
        if (Menu.EntryCount != 0) {
            MenuExit = RunGenericMenu(&Menu, Style, &DefaultEntry, &ChosenOption);
            if (ChosenOption && MenuExit == MENU_EXIT_ENTER) {
                Operation = EFI_BOOT_OPTION_MAKE_DEFAULT;
                *BootOrderNum = ChosenOption->Row;
            } else if (ChosenOption && MenuExit == MENU_EXIT_HIDE) {
                Operation = EFI_BOOT_OPTION_DELETE;
                *BootOrderNum = ChosenOption->Row;
            }
        }
    } else {
        DisplaySimpleMessage(L"Information", L"EFI boot order list is unavailable");
    }

    for (i = 0; i < Menu.EntryCount; ++i) {
        MyFreePool(Menu.Entries[i]->Title);
        MyFreePool(Menu.Entries[i]);
    }
    MyFreePool(Menu.Entries);
    MyFreePool(Menu.InfoLines);
    return Operation;
}  

/*
 * Prune stale BootOrder numbers after a Boot#### deletion. Only EFI_NOT_FOUND
 * means an option is absent: a read failure must not be interpreted as a
 * missing entry. Build a replacement entirely in memory and publish it only
 * after inspecting every record. Leave BootOrder intact on failure, including
 * the case where no remaining referenced option could be read.
 */
static EFI_STATUS DeleteInvalidBootEntries(VOID) {
    EFI_STATUS Status;
    UINTN OrderBytes = 0, EntryBytes = 0, ListSize, i, j = 0;
    UINT16 *BootOrder = NULL, *NewBootOrder = NULL;
    CHAR8 *Contents = NULL;
    CHAR16 *VarName = NULL;

    Status = EfivarGetRaw(&GlobalGuid, L"BootOrder",
                          (CHAR8 **)&BootOrder, &OrderBytes);
    if (EFI_ERROR(Status))
        return Status;
    if (!BootOrder || OrderBytes == 0 || (OrderBytes % sizeof(UINT16))) {
        MyFreePool(BootOrder);
        return EFI_COMPROMISED_DATA;
    }
    ListSize = OrderBytes / sizeof(UINT16);
    NewBootOrder = AllocateZeroPool(OrderBytes);
    if (!NewBootOrder) {
        MyFreePool(BootOrder);
        return EFI_OUT_OF_RESOURCES;
    }
    for (i = 0; i < ListSize; ++i) {
        VarName = PoolPrint(L"Boot%04x", BootOrder[i]);
        if (!VarName) {
            Status = EFI_OUT_OF_RESOURCES;
            goto Done;
        }
        Contents = NULL;
        EntryBytes = 0;
        Status = EfivarGetRaw(&GlobalGuid, VarName, &Contents, &EntryBytes);
        MyFreePool(VarName);
        VarName = NULL;
        MyFreePool(Contents);
        Contents = NULL;
        if (Status == EFI_NOT_FOUND)
            continue;
        if (EFI_ERROR(Status))
            goto Done;
        NewBootOrder[j++] = BootOrder[i];
    }
    Status = EFI_SUCCESS;
    if (j == 0)
        Status = EFI_COMPROMISED_DATA;
    else if (j != ListSize)
        Status = EfivarSetRaw(&GlobalGuid, L"BootOrder",
                             (CHAR8 *)NewBootOrder, j * sizeof(UINT16), TRUE);
Done:
    MyFreePool(Contents);
    MyFreePool(VarName);
    MyFreePool(NewBootOrder);
    MyFreePool(BootOrder);
    return Status;
}

/*
 * Apply a selected firmware-variable mutation after the menu returns. A
 * Boot#### delete can succeed even if subsequent BootOrder cleanup fails;
 * report those outcomes separately. Never label a failed SetVariable write
 * as successful. The parsed options are borrowed for display, then freed.
 */
VOID ManageBootorder(VOID) {
    BOOT_ENTRY_LIST *Entries;
    UINTN BootNum = 0, Operation;
    EFI_STATUS Status, CleanupStatus;
    CHAR16 *Name = NULL, *Message = NULL;

    LOG(1, LOG_LINE_NORMAL, L"Managing boot order list");
    Entries = FindBootOrderEntries();
    Operation = PickOneBootOption(Entries, &BootNum);
    if (Operation == EFI_BOOT_OPTION_DELETE) {
        Name = PoolPrint(L"Boot%04x", BootNum);
        Status = Name ? EfivarSetRaw(&GlobalGuid, Name, NULL, 0, TRUE)
                      : EFI_OUT_OF_RESOURCES;
        if (EFI_ERROR(Status)) {
            Message = PoolPrint(L"Boot%04x could not be deleted (EFI status %lx).",
                                BootNum, (UINTN)Status);
        } else {
            CleanupStatus = DeleteInvalidBootEntries();
            if (EFI_ERROR(CleanupStatus))
                Message = PoolPrint(L"Boot%04x deleted, but BootOrder cleanup failed (EFI status %lx).",
                                    BootNum, (UINTN)CleanupStatus);
            else
                Message = PoolPrint(L"Boot%04x has been deleted.", BootNum);
        }
        DisplaySimpleMessage(L"Information",
                             Message ? Message : L"BOOT could not report the firmware operation.");
        MyFreePool(Name);
        MyFreePool(Message);
    } else if (Operation == EFI_BOOT_OPTION_MAKE_DEFAULT) {
        Status = SetBootDefault(BootNum);
        if (EFI_ERROR(Status))
            Message = PoolPrint(L"Boot%04x could not be selected (EFI status %lx).",
                                BootNum, (UINTN)Status);
        else
            Message = PoolPrint(L"Boot%04x is now the default EFI boot option.", BootNum);
        DisplaySimpleMessage(L"Information",
                             Message ? Message : L"BOOT could not report the firmware operation.");
        MyFreePool(Message);
    }
    DeleteBootOrderEntries(Entries);
}  
