/*
 * Linux EFI-stub kernel discovery and boot option assembly.
 * Kernel versions, initrd candidate names and distribution hints are
 * heuristics rather than a guarantee of a bootable kernel/initramfs pair.
 * The selected kernel, options and volume must remain consistent.
 */
 


































 






 














#include "global.h"
#include "config.h"
#include "lib.h"
#include "menu.h"
#include "mystrings.h"
#include "linux.h"
#include "log.h"
#include "scan.h"

 
 
 
 
 
 
 
 
 
 
 
 
 
/*
 * Chooses an initrd by extracted version fragments and filename similarity.
 * Where multiple names match, its tie-breaks are heuristic, not an authority
 * on which initramfs actually corresponds to a particular installed kernel.
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

 
 
 
 
 
 
 
 
/*
 * Returns a newly prepared options string, substituting version tokens or
 * appending initrd= only when applicable. The caller owns its allocation.
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

 
 
 
/*
 * Resolves main kernel load options from available configuration and defaults.
 * A missing root parameter or inaccessible initrd can still make boot fail.
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

 
 
 
static VOID ParseReleaseFile(CHAR16 **OSIconName, REFIT_VOLUME *Volume, CHAR16 *FileName) {
    UINTN       FileSize = 0;
    REFIT_FILE  File;
    CHAR16      **TokenList;
    UINTN       TokenCount = 0;

    if ((Volume == NULL) || (FileName == NULL) || (OSIconName == NULL) || (*OSIconName == NULL))
        return;

    if (FileExists(Volume->RootDir, FileName) &&
        (ReadFile(Volume->RootDir, FileName, &File, &FileSize) == EFI_SUCCESS)) {
        do {
            TokenCount = ReadTokenLine(&File, &TokenList);
            if ((TokenCount > 1) && (MyStriCmp(TokenList[0], L"ID") ||
                                     MyStriCmp(TokenList[0], L"NAME") ||
                                     MyStriCmp(TokenList[0], L"DISTRIB_ID"))) {
                MergeWords(OSIconName, TokenList[1], L',');
            }  
            FreeTokenLine(&TokenList, &TokenCount);
        } while (TokenCount > 0);
        MyFreePool(File.Buffer);
    }  
}  

 
 
VOID GuessLinuxDistribution(CHAR16 **OSIconName, REFIT_VOLUME *Volume, CHAR16 *LoaderPath) {
     
    ParseReleaseFile(OSIconName, Volume, L"etc\\lsb-release");
    ParseReleaseFile(OSIconName, Volume, L"etc\\os-release");

     
    if (StriSubCmp(L".fc", LoaderPath))
        MergeStrings(OSIconName, L"fedora", L',');
    if (StriSubCmp(L".el", LoaderPath))
        MergeStrings(OSIconName, L"redhat", L',');
}  

 
/*
 * Groups additional Linux kernels as alternate launch entries while retaining
 * the volume context; folding entries must not erase independently bootable OSs.
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
