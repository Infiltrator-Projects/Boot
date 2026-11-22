/* Linux EFI-stub discovery and option construction.
 * Kernel versions, distribution labels and initrd matches are heuristics.
 * The selected volume and command line are resolved at dispatch.
 */

#include "global.h"
#include "config.h"
#include "lib.h"
#include "menu.h"
#include "mystrings.h"
#include "linux.h"
#include "log.h"
#include "scan.h"

/**
 * Search the kernel's directory for init* and booster* names with the same
 * extracted version, allowing two unversioned names to match. With multiple
 * candidates, compare their common prefix starting at the version substring and
 * prefer the shorter full path on a tie. A caller owns the returned path, which
 * survives deletion of the temporary candidate list.
 */
CHAR16 * FindInitrd(IN CHAR16 *LoaderPath, IN REFIT_VOLUME *Volume) {
    CHAR16              *InitrdName = NULL, *FileName, *KernelVersion, *InitrdVersion, *Path;
    CHAR16              *KernelPostNum, *InitrdPostNum;
    UINTN               MaxSharedChars, SharedChars;
    STRING_LIST         *InitrdNames = NULL, *FinalInitrdName = NULL, *CurrentInitrdName = NULL, *MaxSharedInitrd;
    REFIT_DIR_ITER      DirIter;
    EFI_FILE_INFO       *DirEntry;

    LOG(1, LOG_LINE_NORMAL, L"Searching for an initrd to match '%s' on '%s'", LoaderPath, Volume->VolName);
    FileName = Basename(LoaderPath);
    KernelVersion = FindNumbers(FileName);
    LOG(3, LOG_LINE_NORMAL, L"Kernel version string is '%s'", KernelVersion);
    Path = FindPath(LoaderPath);

    if (StrLen(Path) == 0) {
        MergeStrings(&Path, L"\\", 0);
    }  
    DirIterOpen(Volume->RootDir, Path, &DirIter);

    if ((StrLen(Path) > 0) && (Path[StrLen(Path) - 1] != L'\\'))
        MergeStrings(&Path, L"\\", 0);
    while (DirIterNext(&DirIter, 2, L"init*,booster*", &DirEntry)) {
        InitrdVersion = FindNumbers(DirEntry->FileName);
        if (((KernelVersion != NULL) && (MyStriCmp(InitrdVersion, KernelVersion))) ||
            ((KernelVersion == NULL) && (InitrdVersion == NULL))) {
                CurrentInitrdName = AllocateZeroPool(sizeof(STRING_LIST));
                if (InitrdNames == NULL)
                    InitrdNames = FinalInitrdName = CurrentInitrdName;
                if (CurrentInitrdName) {
                    CurrentInitrdName->Value = PoolPrint(L"%s%s", Path, DirEntry->FileName);
                    if (CurrentInitrdName != FinalInitrdName) {
                        FinalInitrdName->Next = CurrentInitrdName;
                        FinalInitrdName = CurrentInitrdName;
                    }  
                }  
        }  
        MyFreePool(InitrdVersion);
    }  
    if (InitrdNames) {
        if (InitrdNames->Next == NULL) {
            InitrdName = StrDuplicate(InitrdNames -> Value);
        } else {
            /*
             * Candidates already match the parsed version. Resolve flavour
             * suffixes by the length of the common prefix starting at the
             * version, then favour the shorter candidate path on a tie.
             * Directory enumeration order alone cannot distinguish kernel
             * flavours.
             */
            MaxSharedInitrd = CurrentInitrdName = InitrdNames;
            MaxSharedChars = 0;
            while (CurrentInitrdName != NULL) {
                KernelPostNum = MyStrStr(LoaderPath, KernelVersion);
                InitrdPostNum = MyStrStr(CurrentInitrdName->Value, KernelVersion);
                SharedChars = NumCharsInCommon(KernelPostNum, InitrdPostNum);
                if (SharedChars > MaxSharedChars || (SharedChars == MaxSharedChars && StrLen(CurrentInitrdName->Value) < StrLen(MaxSharedInitrd->Value))) {
                    MaxSharedChars = SharedChars;
                    MaxSharedInitrd = CurrentInitrdName;
                }  

                CurrentInitrdName = CurrentInitrdName->Next;
            }
            if (MaxSharedInitrd)
                InitrdName = StrDuplicate(MaxSharedInitrd->Value);
        }  
    }  
    DeleteStringList(InitrdNames);

    MyFreePool(KernelVersion);
    MyFreePool(Path);
    MyFreePool(FileName);
    LOG(1, LOG_LINE_NORMAL, L"Located initrd is '%s'", InitrdName);
    return (InitrdName);
}  

/**
 * Work on an owned copy of the options. With an initrd present, a %v placeholder
 * takes the substitution branch. Otherwise append one initrd= argument only when
 * the options lack that substring. Thus an explicit initrd path in a stanza takes
 * precedence over automatic discovery.
 * The input Options is borrowed; the returned string (if any) is separately
 * allocated and belongs to the caller. The implementation tests Options for
 * NULL only before duplication, not before the later StriSubCmp calls when
 * InitrdPath is non-NULL. Such a call is not established as safe. Substring
 * presence does not validate the discovered initrd file's readability.
 */
CHAR16 *AddInitrdToOptions(CHAR16 *Options, CHAR16 *InitrdPath) {
    CHAR16 *NewOptions = NULL;

    if (Options != NULL)
        NewOptions = StrDuplicate(Options);

    if (InitrdPath != NULL) {
        if (StriSubCmp(L"%v", Options)) {
            CHAR16 *InitrdVersion = FindNumbers(InitrdPath);
            ReplaceSubstring(&NewOptions, L"%v", InitrdVersion);

            MyFreePool(InitrdVersion);
        } else if (!StriSubCmp(L"initrd=", Options)) {
            MergeStrings(&NewOptions, L"initrd=", L' ');
            MergeStrings(&NewOptions, InitrdPath, 0);
        }
    }
    return NewOptions;
}  

/**
 * Combine the first options-file entry with the initrd chosen for this kernel.
 * The version substitution here is derived from the initrd name. Release the
 * three intermediate strings after AddInitrdToOptions has produced the
 * independently owned result.
 */
CHAR16 * GetMainLinuxOptions(IN CHAR16 * LoaderPath, IN REFIT_VOLUME *Volume) {
    CHAR16 *Options = NULL, *InitrdName, *FullOptions = NULL, *KernelVersion;

    Options = GetFirstOptionsFromFile(LoaderPath, Volume);
    InitrdName = FindInitrd(LoaderPath, Volume);
    KernelVersion = FindNumbers(InitrdName);
    ReplaceSubstring(&Options, KERNEL_VERSION, KernelVersion);
    FullOptions = AddInitrdToOptions(Options, InitrdName);

    MyFreePool(Options);
    MyFreePool(InitrdName);
    MyFreePool(KernelVersion);
    return (FullOptions);
}  

/*
 * Interprets distribution identification data read from a candidate volume
 * to refine menu artwork. These labels are heuristic presentation metadata,
 * not executable provenance or operating-system identity attestation.
 */
/*
 * Consume all fields of os-release/lsb-release. FreeTokenLine() resets
 * TokenCount, so the loop condition must read a NEW line before deciding to
 * stop. Distribution IDs outrank generic NAME and ID_LIKE tokens: otherwise
 * Mint's NAME="Linux Mint" produces os_linux before ID=linuxmint can win.
 *
 * ReadTokenLine owns its tokens, while the merged icon-hint strings own copies.
 * The release file is informational; a malformed/missing file must not prevent
 * fallback to the generic Linux icon or affect loader execution.
 */
static VOID ParseReleaseFile(CHAR16 **OSIconName, REFIT_VOLUME *Volume, CHAR16 *FileName) {
    UINTN       FileSize = 0;
    REFIT_FILE  File;
    CHAR16      **TokenList = NULL;
    UINTN       TokenCount;
    CHAR16      *SimilarIDs = NULL, *DisplayNames = NULL;

    if (!Volume || !Volume->RootDir || !FileName || !OSIconName)
        return;

    if (FileExists(Volume->RootDir, FileName) &&
        (ReadFile(Volume->RootDir, FileName, &File, &FileSize) == EFI_SUCCESS)) {
        while ((TokenCount = ReadTokenLine(&File, &TokenList)) > 0) {
            if (TokenCount > 1) {
                if (MyStriCmp(TokenList[0], L"ID") ||
                    MyStriCmp(TokenList[0], L"DISTRIB_ID")) {
                    MergeWords(OSIconName, TokenList[1], L',');
                } else if (MyStriCmp(TokenList[0], L"ID_LIKE")) {
                    MergeWords(&SimilarIDs, TokenList[1], L',');
                } else if (MyStriCmp(TokenList[0], L"NAME")) {
                    MergeWords(&DisplayNames, TokenList[1], L',');
                }
            }
            FreeTokenLine(&TokenList, &TokenCount);
        }
        /* ID_LIKE is more specific than the word "Linux" in NAME. */
        if (SimilarIDs)
            MergeStrings(OSIconName, SimilarIDs, L',');
        if (DisplayNames)
            MergeStrings(OSIconName, DisplayNames, L',');
        MyFreePool(SimilarIDs);
        MyFreePool(DisplayNames);
        MyFreePool(File.Buffer);
    }
}

/*
 * Merge os-release/lsb-release and kernel-filename hints into icon labels.
 * Fedora/Red Hat suffix recognition is heuristic and can coexist with other
 * labels; no signature, root filesystem identity or bootability check occurs.
 * OSIconName is an in/out string allocation managed by the string helpers.
 */
VOID GuessLinuxDistribution(CHAR16 **OSIconName, REFIT_VOLUME *Volume, CHAR16 *LoaderPath) {

    ParseReleaseFile(OSIconName, Volume, L"etc\\lsb-release");
    ParseReleaseFile(OSIconName, Volume, L"etc\\os-release");

    if (StriSubCmp(L".fc", LoaderPath))
        MergeStrings(OSIconName, L"fedora", L',');
    if (StriSubCmp(L".el", LoaderPath))
        MergeStrings(OSIconName, L"redhat", L',');
}  

/**
 * Use the first kernel's options file as the template for its folded siblings.
 * For each label/options pair, create a child carrying this sibling's path,
 * version and selected initrd. This preserves a separately launchable entry for
 * each kernel under one top-level icon.
 */
VOID AddKernelToSubmenu(LOADER_ENTRY * TargetLoader, CHAR16 *FileName, REFIT_VOLUME *Volume) {
    REFIT_FILE          *File;
    CHAR16              **TokenList = NULL, *InitrdName, *SubmenuName = NULL, *VolName = NULL;
    CHAR16              *Path = NULL, *Title, *KernelVersion;
    REFIT_MENU_SCREEN   *SubScreen;
    LOADER_ENTRY        *SubEntry;
    UINTN               TokenCount;

    File = ReadLinuxOptionsFile(TargetLoader->LoaderPath, Volume);
    if (File != NULL) {
        SubScreen = TargetLoader->me.SubScreen;
        InitrdName = FindInitrd(FileName, Volume);
        KernelVersion = FindNumbers(FileName);
        while ((TokenCount = ReadTokenLine(File, &TokenList)) > 1) {
            ReplaceSubstring(&(TokenList[1]), KERNEL_VERSION, KernelVersion);
            SubEntry = InitializeLoaderEntry(TargetLoader);
            SplitPathName(FileName, &VolName, &Path, &SubmenuName);
            MergeStrings(&SubmenuName, L": ", '\0');
            MergeStrings(&SubmenuName, TokenList[0] ? StrDuplicate(TokenList[0]) : StrDuplicate(L"Boot Linux"), '\0');
            Title = StrDuplicate(SubmenuName);
            LimitStringLength(Title, MAX_LINE_LENGTH);
            SubEntry->me.Title = Title;
            MyFreePool(SubEntry->LoadOptions);
            SubEntry->LoadOptions = AddInitrdToOptions(TokenList[1], InitrdName);
            MyFreePool(SubEntry->LoaderPath);
            SubEntry->LoaderPath = StrDuplicate(FileName);
            CleanUpPathNameSlashes(SubEntry->LoaderPath);
            SubEntry->Volume = Volume;
            FreeTokenLine(&TokenList, &TokenCount);
            SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_LINUX;
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        }  
        MyFreePool(VolName);
        MyFreePool(Path);
        MyFreePool(SubmenuName);
        MyFreePool(InitrdName);
        MyFreePool(File);
        MyFreePool(KernelVersion);
    }  
}  

/*
 * Searches for an alternative filename associated with a signed loader.
 * Naming alone does not verify a digital signature: firmware authentication
 * still occurs at the subsequent EFI image-loading boundary.
 */
BOOLEAN HasSignedCounterpart(IN REFIT_VOLUME *Volume, IN CHAR16 *FullName) {
    CHAR16 *NewFile = NULL;
    BOOLEAN retval = FALSE;

    MergeStrings(&NewFile, FullName, 0);
    MergeStrings(&NewFile, L".efi.signed", 0);
    if (NewFile != NULL) {
        if (FileExists(Volume->RootDir, NewFile)) {
            LOG(2, LOG_LINE_NORMAL, L"Found signed counterpart to '%s'", FullName);
            retval = TRUE;
        }
        MyFreePool(NewFile);
    }  

    return retval;
}  
