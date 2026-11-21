/*
 * Dynamic volume and EFI loader discovery. Readable firmware volumes,
 * exclusions, loader header checks and fallback de-duplication determine
 * which choices are displayed. Hidden menu entries are not deleted from disk.
 * Every supported installation should remain discoverable, not a fixed pair.
 */
 


































 






 














#include "global.h"
#include "config.h"
#include "screen.h"
#include "lib.h"
#include "icns.h"
#include "menu.h"
#include "mok.h"
#include "apple.h"
#include "mystrings.h"
#include "security_policy.h"
#include "driver_support.h"
#include "launch_efi.h"
#include "launch_legacy.h"
#include "linux.h"
#include "log.h"
#include "scan.h"
#include "install.h"
#include "../include/refit_call_wrapper.h"


 
 

#define MACOSX_LOADER_DIR      L"System\\Library\\CoreServices"
#define MACOSX_LOADER_PATH      ( MACOSX_LOADER_DIR L"\\boot.efi" )
#if defined (EFIX64)
#define GPTSYNC_NAMES           L"gptsync.efi,gptsync_x64.efi"
#define GDISK_NAMES             L"gdisk.efi,gdisk_x64.efi"
#define NETBOOT_NAMES           L"ipxe.efi"
#define FALLBACK_FULLNAME       L"EFI\\BOOT\\bootx64.efi"
#define FALLBACK_BASENAME       L"bootx64.efi"
#elif defined (EFI32)
#define GPTSYNC_NAMES           L"gptsync.efi,gptsync_ia32.efi"
#define GDISK_NAMES             L"gdisk.efi,gdisk_ia32.efi"
#define NETBOOT_NAMES           L"ipxe.efi"
#define FALLBACK_FULLNAME       L"EFI\\BOOT\\bootia32.efi"
#define FALLBACK_BASENAME       L"bootia32.efi"
#elif defined (EFIAARCH64)
#define GPTSYNC_NAMES           L"gptsync.efi,gptsync_aa64.efi"
#define GDISK_NAMES             L"gdisk.efi,gdisk_aa64.efi"
#define NETBOOT_NAMES           L"ipxe.efi"
#define FALLBACK_FULLNAME       L"EFI\\BOOT\\bootaa64.efi"
#define FALLBACK_BASENAME       L"bootaa64.efi"
#else
#define GPTSYNC_NAMES           L"gptsync.efi"
#define GDISK_NAMES             L"gdisk.efi"
#define NETBOOT_NAMES           L"ipxe.efi"
#define DRIVER_DIRS             L"drivers"
#define FALLBACK_FULLNAME       L"EFI\\BOOT\\boot.efi"  
#define FALLBACK_BASENAME       L"boot.efi"             

#endif

#define IPXE_DISCOVER_NAME      L"\\efi\\tools\\ipxe_discover.efi"
#define IPXE_NAME               L"\\efi\\tools\\ipxe.efi"

EFI_GUID GlobalGuid = EFI_GLOBAL_VARIABLE;

static REFIT_MENU_ENTRY MenuEntryAbout    = { L"About BOOT", TAG_ABOUT, 1, 0, 'A', NULL, NULL, NULL };
static REFIT_MENU_ENTRY MenuEntryReset    = { L"Reboot Computer", TAG_REBOOT, 1, 0, 'R', NULL, NULL, NULL };
static REFIT_MENU_ENTRY MenuEntryShutdown = { L"Shut Down Computer", TAG_SHUTDOWN, 1, 0, 'U', NULL, NULL, NULL };
static REFIT_MENU_ENTRY MenuEntryRotateCsr = { L"Change SIP Policy", TAG_CSR_ROTATE, 1, 0, 0, NULL, NULL, NULL };
static REFIT_MENU_ENTRY MenuEntryFirmware = { L"Reboot to Computer Setup Utility", TAG_FIRMWARE, 1, 0, 0, NULL, NULL, NULL };
static REFIT_MENU_ENTRY MenuEntryManageHidden = { L"Manage Hidden Tags Menu", TAG_HIDDEN, 1, 0, 0, NULL, NULL, NULL };
static REFIT_MENU_ENTRY MenuEntryBootorder = { L"Manage EFI boot order", TAG_BOOTORDER, 1, 0, 0, NULL, NULL, NULL };
static REFIT_MENU_ENTRY MenuEntryExit     = { L"Exit BOOT", TAG_EXIT, 1, 0, 0, NULL, NULL, NULL };

 
 
struct LOADER_LIST {
    CHAR16              *FileName;
    EFI_TIME            TimeStamp;
    struct LOADER_LIST  *NextEntry;
};

 
 
 

 
 
static REFIT_MENU_SCREEN* CopyMenuScreen(REFIT_MENU_SCREEN *Entry) {
    REFIT_MENU_SCREEN *NewEntry;
    UINTN i;

    NewEntry = AllocateZeroPool(sizeof(REFIT_MENU_SCREEN));
    if ((Entry != NULL) && (NewEntry != NULL)) {
        NewEntry->Title = (Entry->Title) ? StrDuplicate(Entry->Title) : NULL;
        if (Entry->TitleImage != NULL) {
            NewEntry->TitleImage = AllocatePool(sizeof(EG_IMAGE));
            if (NewEntry->TitleImage != NULL)
                MyCopyMem(NewEntry->TitleImage, Entry->TitleImage, sizeof(EG_IMAGE));
        }  
        NewEntry->InfoLineCount = Entry->InfoLineCount;
        NewEntry->InfoLines = (CHAR16**) AllocateZeroPool(Entry->InfoLineCount * (sizeof(CHAR16*)));
        for (i = 0; i < Entry->InfoLineCount && NewEntry->InfoLines; i++) {
            NewEntry->InfoLines[i] = (Entry->InfoLines[i]) ? StrDuplicate(Entry->InfoLines[i]) : NULL;
        }  
        NewEntry->EntryCount = Entry->EntryCount;
        NewEntry->Entries = (REFIT_MENU_ENTRY**) AllocateZeroPool(Entry->EntryCount * (sizeof (REFIT_MENU_ENTRY*)));
        for (i = 0; i < Entry->EntryCount && NewEntry->Entries; i++) {
            AddMenuEntry(NewEntry, Entry->Entries[i]);
        }  
        NewEntry->TimeoutSeconds = Entry->TimeoutSeconds;
        NewEntry->TimeoutText = (Entry->TimeoutText) ? StrDuplicate(Entry->TimeoutText) : NULL;
        NewEntry->Hint1 = (Entry->Hint1) ? StrDuplicate(Entry->Hint1) : NULL;
        NewEntry->Hint2 = (Entry->Hint2) ? StrDuplicate(Entry->Hint2) : NULL;
    }  
    return (NewEntry);
}  

 
 
 
 
 
static REFIT_MENU_ENTRY* CopyMenuEntry(REFIT_MENU_ENTRY *Entry) {
    REFIT_MENU_ENTRY *NewEntry;

    NewEntry = AllocateZeroPool(sizeof(REFIT_MENU_ENTRY));
    if ((Entry != NULL) && (NewEntry != NULL)) {
        MyCopyMem(NewEntry, Entry, sizeof(REFIT_MENU_ENTRY));
        NewEntry->Title = (Entry->Title) ? StrDuplicate(Entry->Title) : NULL;
        if (Entry->BadgeImage != NULL) {
            NewEntry->BadgeImage = AllocatePool(sizeof(EG_IMAGE));
            if (NewEntry->BadgeImage != NULL)
                MyCopyMem(NewEntry->BadgeImage, Entry->BadgeImage, sizeof(EG_IMAGE));
        }
        if (Entry->Image != NULL) {
            NewEntry->Image = AllocatePool(sizeof(EG_IMAGE));
            if (NewEntry->Image != NULL)
                MyCopyMem(NewEntry->Image, Entry->Image, sizeof(EG_IMAGE));
        }
        if (Entry->SubScreen != NULL) {
            NewEntry->SubScreen = CopyMenuScreen(Entry->SubScreen);
        }
    }  
    return (NewEntry);
}  

 
 
 
 
 
/*
 * Establishes a complete loader-entry baseline before discovery-specific
 * fields are assigned. Later scanning code depends on this initial state
 * when constructing alternate launch paths and nested menu entries.
 */
LOADER_ENTRY *InitializeLoaderEntry(IN LOADER_ENTRY *Entry) {
    LOADER_ENTRY *NewEntry = NULL;

    NewEntry = AllocateZeroPool(sizeof(LOADER_ENTRY));
    if (NewEntry != NULL) {
        NewEntry->me.Title        = NULL;
        NewEntry->me.Tag          = TAG_LOADER;
        NewEntry->Enabled         = TRUE;
        NewEntry->UseGraphicsMode = FALSE;
        NewEntry->OSType          = 0;
        NewEntry->EfiLoaderPath   = NULL;
        NewEntry->EfiBootNum      = 0;
        if (Entry != NULL) {
            NewEntry->LoaderPath      = (Entry->LoaderPath) ? StrDuplicate(Entry->LoaderPath) : NULL;
            NewEntry->Volume          = Entry->Volume;
            NewEntry->UseGraphicsMode = Entry->UseGraphicsMode;
            NewEntry->LoadOptions     = (Entry->LoadOptions) ? StrDuplicate(Entry->LoadOptions) : NULL;
            NewEntry->InitrdPath      = (Entry->InitrdPath) ? StrDuplicate(Entry->InitrdPath) : NULL;
            NewEntry->EfiLoaderPath   = (Entry->EfiLoaderPath) ? DuplicateDevicePath(Entry->EfiLoaderPath) : NULL;
            NewEntry->EfiBootNum      = Entry->EfiBootNum;
        }
    }  
    return (NewEntry);
}  

 
 
 
 
 
 
 
REFIT_MENU_SCREEN *InitializeSubScreen(IN LOADER_ENTRY *Entry) {
    CHAR16              *FileName, *MainOptions = NULL;
    REFIT_MENU_SCREEN   *SubScreen = NULL;
    LOADER_ENTRY        *SubEntry;

    FileName = Basename(Entry->LoaderPath);
    if (Entry->me.SubScreen == NULL) {  
        SubScreen = AllocateZeroPool(sizeof(REFIT_MENU_SCREEN));
        if (SubScreen != NULL) {
            SubScreen->Title = PoolPrint(L"Boot Options for %s on %s",
                                         (Entry->Title != NULL) ? Entry->Title : FileName,
                                         Entry->Volume->VolName);
            LOG(2, LOG_LINE_NORMAL, L"Creating subscreen '%s'", SubScreen->Title);
            SubScreen->TitleImage = Entry->me.Image;
             
            SubEntry = InitializeLoaderEntry(Entry);
            if (SubEntry != NULL) {
                LOG(2, LOG_LINE_NORMAL, L"Creating loader entry for '%s'", SubScreen->Title);
                SubEntry->me.Title = StrDuplicate(L"Boot using default options");
                MainOptions = SubEntry->LoadOptions;
                SubEntry->LoadOptions = AddInitrdToOptions(MainOptions, SubEntry->InitrdPath);
                MyFreePool(MainOptions);
                AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
            }  
            SubScreen->Hint1 = StrDuplicate(SUBSCREEN_HINT1);
            if (GlobalConfig.HideUIFlags & HIDEUI_FLAG_EDITOR) {
                SubScreen->Hint2 = StrDuplicate(SUBSCREEN_HINT2_NO_EDITOR);
            } else {
                SubScreen->Hint2 = StrDuplicate(SUBSCREEN_HINT2);
            }  
        }  
    } else {  
        SubScreen = Entry->me.SubScreen;
    }  
    MyFreePool(FileName);
    return SubScreen;
}  

VOID GenerateSubScreen(LOADER_ENTRY *Entry, IN REFIT_VOLUME *Volume, IN BOOLEAN GenerateReturn) {
    REFIT_MENU_SCREEN  *SubScreen;
    LOADER_ENTRY       *SubEntry;
    CHAR16             *InitrdName, *KernelVersion = NULL;
    CHAR16             DiagsFileName[256];
    REFIT_FILE         *File;
    UINTN              TokenCount;
    CHAR16             **TokenList;

     
    if (StrLen(Entry->Title) == 0) {
        MyFreePool(Entry->Title);
        Entry->Title = NULL;
    }
    SubScreen = InitializeSubScreen(Entry);

     
    if (Entry->OSType == 'M') {           
#if defined(EFIX64)
        SubEntry = InitializeLoaderEntry(Entry);
        if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Boot macOS with a 64-bit kernel";
            SubEntry->LoadOptions     = L"arch=x86_64";
            SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_OSX;
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        }  

        SubEntry = InitializeLoaderEntry(Entry);
        if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Boot macOS with a 32-bit kernel";
            SubEntry->LoadOptions     = L"arch=i386";
            SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_OSX;
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        }  
#endif

        if (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_SINGLEUSER)) {
            SubEntry = InitializeLoaderEntry(Entry);
            if (SubEntry != NULL) {
                SubEntry->me.Title        = L"Boot macOS in verbose mode";
                SubEntry->UseGraphicsMode = FALSE;
                SubEntry->LoadOptions     = L"-v";
                AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
            }  

#if defined(EFIX64)
            SubEntry = InitializeLoaderEntry(Entry);
            if (SubEntry != NULL) {
                SubEntry->me.Title        = L"Boot macOS in verbose mode (64-bit)";
                SubEntry->UseGraphicsMode = FALSE;
                SubEntry->LoadOptions     = L"-v arch=x86_64";
                AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
            }

            SubEntry = InitializeLoaderEntry(Entry);
            if (SubEntry != NULL) {
                SubEntry->me.Title        = L"Boot macOS in verbose mode (32-bit)";
                SubEntry->UseGraphicsMode = FALSE;
                SubEntry->LoadOptions     = L"-v arch=i386";
                AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
            }
#endif

            SubEntry = InitializeLoaderEntry(Entry);
            if (SubEntry != NULL) {
                SubEntry->me.Title        = L"Boot macOS in single user mode";
                SubEntry->UseGraphicsMode = FALSE;
                SubEntry->LoadOptions     = L"-v -s";
                AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
            }  
        }  

        if (!(GlobalConfig.HideUIFlags & HIDEUI_FLAG_SAFEMODE)) {
            SubEntry = InitializeLoaderEntry(Entry);
            if (SubEntry != NULL) {
                SubEntry->me.Title        = L"Boot macOS in safe mode";
                SubEntry->UseGraphicsMode = FALSE;
                SubEntry->LoadOptions     = L"-v -x";
                AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
            }  
        }  

         
        StrCpy(DiagsFileName, L"System\\Library\\CoreServices\\.diagnostics\\diags.efi");
        if (FileExists(Volume->RootDir, DiagsFileName) && !(GlobalConfig.HideUIFlags & HIDEUI_FLAG_HWTEST)) {
            SubEntry = InitializeLoaderEntry(Entry);
            if (SubEntry != NULL) {
                SubEntry->me.Title        = L"Run Apple Hardware Test";
                MyFreePool(SubEntry->LoaderPath);
                SubEntry->LoaderPath      = StrDuplicate(DiagsFileName);
                SubEntry->Volume          = Volume;
                SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_OSX;
                AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
            }  
        }  

    } else if (Entry->OSType == 'L') {    
        File = ReadLinuxOptionsFile(Entry->LoaderPath, Volume);
        if (File != NULL) {
            InitrdName =  FindInitrd(Entry->LoaderPath, Volume);
            TokenCount = ReadTokenLine(File, &TokenList);
            KernelVersion = FindNumbers(Entry->LoaderPath);
            if (TokenCount >= 2) {
                ReplaceSubstring(&(TokenList[1]), KERNEL_VERSION, KernelVersion);
            }
             
             
             
            if ((TokenCount > 1) && (SubScreen->Entries != NULL) && (SubScreen->Entries[0] != NULL)) {
                MyFreePool(SubScreen->Entries[0]->Title);
                SubScreen->Entries[0]->Title = TokenList[0] ? StrDuplicate(TokenList[0]) : StrDuplicate(L"Boot Linux");
            }  
            FreeTokenLine(&TokenList, &TokenCount);
            while ((TokenCount = ReadTokenLine(File, &TokenList)) > 1) {
                ReplaceSubstring(&(TokenList[1]), KERNEL_VERSION, KernelVersion);
                SubEntry = InitializeLoaderEntry(Entry);
                SubEntry->me.Title = TokenList[0] ? StrDuplicate(TokenList[0]) : StrDuplicate(L"Boot Linux");
                MyFreePool(SubEntry->LoadOptions);
                SubEntry->LoadOptions = AddInitrdToOptions(TokenList[1], InitrdName);
                FreeTokenLine(&TokenList, &TokenCount);
                SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_LINUX;
                AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
            }  
            MyFreePool(KernelVersion);
            MyFreePool(InitrdName);
            MyFreePool(File);
        }  

    } else if (Entry->OSType == 'E') {    
        SubEntry = InitializeLoaderEntry(Entry);
        if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Run ELILO in interactive mode";
            SubEntry->LoadOptions     = L"-p";
            SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_ELILO;
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        }

        SubEntry = InitializeLoaderEntry(Entry);
        if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Boot Linux for a 17\" iMac or a 15\" MacBook Pro (*)";
            SubEntry->LoadOptions     = L"-d 0 i17";
            SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_ELILO;
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        }

        SubEntry = InitializeLoaderEntry(Entry);
        if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Boot Linux for a 20\" iMac (*)";
            SubEntry->LoadOptions     = L"-d 0 i20";
            SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_ELILO;
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        }

        SubEntry = InitializeLoaderEntry(Entry);
        if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Boot Linux for a Mac Mini (*)";
            SubEntry->LoadOptions     = L"-d 0 mini";
            SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_ELILO;
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        }

        AddMenuInfoLine(SubScreen, L"NOTE: This is an example. Entries");
        AddMenuInfoLine(SubScreen, L"marked with (*) may not work.");

    } else if (Entry->OSType == 'X') {    
         
        Entry->LoadOptions = L"-s -h";

        SubEntry = InitializeLoaderEntry(Entry);
        if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Boot Windows from Hard Disk";
            SubEntry->LoadOptions     = L"-s -h";
            SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_WINDOWS;
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        }

        SubEntry = InitializeLoaderEntry(Entry);
        if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Boot Windows from CD-ROM";
            SubEntry->LoadOptions     = L"-s -c";
            SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_WINDOWS;
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        }

        SubEntry = InitializeLoaderEntry(Entry);
        if (SubEntry != NULL) {
            SubEntry->me.Title        = L"Run XOM in text mode";
            SubEntry->LoadOptions     = L"-v";
            SubEntry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_WINDOWS;
            AddMenuEntry(SubScreen, (REFIT_MENU_ENTRY *)SubEntry);
        }
    }  
    if (GenerateReturn)
        AddMenuEntry(SubScreen, &MenuEntryReturn);
    Entry->me.SubScreen = SubScreen;
}  

 
 
 
VOID SetLoaderDefaults(LOADER_ENTRY *Entry, CHAR16 *LoaderPath, REFIT_VOLUME *Volume) {
    CHAR16      *NameClues, *PathOnly, *NoExtension, *OSIconName = NULL, *Temp;
    CHAR16      ShortcutLetter = 0;

    NameClues = Basename(LoaderPath);
    PathOnly = FindPath(LoaderPath);
    NoExtension = StripEfiExtension(NameClues);

    LOG(3, LOG_LINE_NORMAL, L"Finding loader defaults for '%s'", Entry->me.Title);
    if (Volume->DiskKind == DISK_KIND_NET) {
        MergeStrings(&NameClues, Entry->me.Title, L' ');
    } else {
         
         
        LOG(4, LOG_LINE_NORMAL, L"Trying to load icon in same directory as loader....");
        if (!Entry->me.Image) {
            Entry->me.Image = egLoadIconAnyType(Volume->RootDir, PathOnly, NoExtension,
                                                GlobalConfig.IconSizes[ICON_SIZE_BIG]);
        }
        if (!Entry->me.Image) {
            Entry->me.Image = egCopyImage(Volume->VolIconImage);
        }

         
         
        LOG(4, LOG_LINE_NORMAL, L"Creating icon hint based on loader path '%s'", LoaderPath);
        Temp = FindLastDirName(LoaderPath);
        MergeStrings(&OSIconName, Temp, L',');
        MyFreePool(Temp);
        Temp = NULL;
        if (OSIconName != NULL) {
            ShortcutLetter = OSIconName[0];
        }

         
         
         
        LOG(4, LOG_LINE_NORMAL, L"Merging hints based on filesystem name ('%s')", Volume->FsName);
        MergeWords(&OSIconName, Volume->FsName, L',');
        LOG(4, LOG_LINE_NORMAL, L"Merging hints based on partition name ('%s')", Volume->PartName);
        MergeWords(&OSIconName, Volume->PartName, L',');
    }  

    LOG(4, LOG_LINE_NORMAL, L"Adding hints based on specific loaders");
     
    if (IsInSubstring(NameClues, GlobalConfig.LinuxPrefixes)) {
        if (Volume->DiskKind != DISK_KIND_NET) {
            GuessLinuxDistribution(&OSIconName, Volume, LoaderPath);
            Entry->LoadOptions = GetMainLinuxOptions(LoaderPath, Volume);
        }
        MergeStrings(&OSIconName, L"linux", L',');
        Entry->OSType = 'L';
        if (ShortcutLetter == 0)
            ShortcutLetter = 'L';
        Entry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_LINUX;
    } else if (StriSubCmp(L"refit", LoaderPath)) {
        MergeStrings(&OSIconName, L"refit", L',');
        Entry->OSType = 'R';
        ShortcutLetter = 'R';
    } else if (StriSubCmp(L"boot", LoaderPath)) {
        MergeStrings(&OSIconName, L"boot", L',');
        Entry->OSType = 'R';
        ShortcutLetter = 'R';
    } else if (StriSubCmp(MACOSX_LOADER_PATH, LoaderPath)) {
        MergeStrings(&OSIconName, L"mac", L',');
        Entry->OSType = 'M';
        ShortcutLetter = 'M';
        Entry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_OSX;
    } else if (MyStriCmp(NameClues, L"diags.efi")) {
        MergeStrings(&OSIconName, L"hwtest", L',');
    } else if (MyStriCmp(NameClues, L"e.efi") || MyStriCmp(NameClues, L"elilo.efi") || StriSubCmp(L"elilo", NameClues)) {
        MergeStrings(&OSIconName, L"elilo,linux", L',');
        Entry->OSType = 'E';
        if (ShortcutLetter == 0)
            ShortcutLetter = 'L';
        Entry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_ELILO;
    } else if (StriSubCmp(L"grub", NameClues)) {
        MergeStrings(&OSIconName, L"grub,linux", L',');
        Entry->OSType = 'G';
        ShortcutLetter = 'G';
        Entry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_GRUB;
    } else if (MyStriCmp(NameClues, L"cdboot.efi") ||
               MyStriCmp(NameClues, L"bootmgr.efi") ||
               MyStriCmp(NameClues, L"bootmgfw.efi") ||
               MyStriCmp(NameClues, L"bkpbootmgfw.efi")) {
        MergeStrings(&OSIconName, L"win8", L',');
        Entry->OSType = 'W';
        ShortcutLetter = 'W';
        Entry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_WINDOWS;
    } else if (MyStriCmp(NameClues, L"xom.efi")) {
        MergeStrings(&OSIconName, L"xom,win,win8", L',');
        Entry->OSType = 'X';
        ShortcutLetter = 'W';
        Entry->UseGraphicsMode = GlobalConfig.GraphicsFor & GRAPHICS_FOR_WINDOWS;
    }
    else if (StriSubCmp(L"ipxe", NameClues)) {
        Entry->OSType = 'N';
        ShortcutLetter = 'N';
        MergeStrings(&OSIconName, L"network", L',');
    }

    if ((ShortcutLetter >= 'a') && (ShortcutLetter <= 'z'))
        ShortcutLetter = ShortcutLetter - 'a' + 'A';  
    Entry->me.ShortcutLetter = ShortcutLetter;
    if (Entry->me.Image == NULL) {
        LOG(4, LOG_LINE_NORMAL, L"Trying to locate an icon based on hints '%s'", OSIconName);
        Entry->me.Image = LoadOSIcon(OSIconName, L"unknown", FALSE);
    }
    MyFreePool(PathOnly);
    MyFreePool(OSIconName);
    MyFreePool(NoExtension);
    MyFreePool(NameClues);
}  

 
static LOADER_ENTRY * AddEfiLoaderEntry(IN EFI_DEVICE_PATH *EfiLoaderPath,
                                        IN CHAR16 *LoaderTitle,
                                        IN UINT16 EfiBootNum,
                                        IN UINTN Row,
                                        EG_IMAGE *Icon) {
    LOADER_ENTRY  *Entry;
    CHAR16        *OSIconName = NULL;
    CHAR16        *FullTitle = NULL;
    CHAR16        *TempStr;

    Entry = InitializeLoaderEntry(NULL);
    if (Entry) {
        Entry->DiscoveryType = DISCOVERY_TYPE_AUTO;
        if (LoaderTitle)
            FullTitle = PoolPrint(L"Reboot to %s", LoaderTitle);
        Entry->me.Title = StrDuplicate((FullTitle) ? FullTitle : L"Unknown");
        Entry->me.Row = Row;
        Entry->me.Tag = TAG_FIRMWARE_LOADER;
        Entry->Title = StrDuplicate((LoaderTitle) ? LoaderTitle : L"Unknown");  
        Entry->EfiLoaderPath = DuplicateDevicePath(EfiLoaderPath);
        TempStr = DevicePathToStr(EfiLoaderPath);
        LOG(2, LOG_LINE_NORMAL, L"EFI loader path = '%s'", TempStr);
        MyFreePool(TempStr);
        Entry->EfiBootNum = EfiBootNum;
        MergeWords(&OSIconName, Entry->me.Title, L',');
        MergeStrings(&OSIconName, L"unknown", L',');
        if (Icon) {
            Entry->me.Image = Icon;
        } else {
            Entry->me.Image = LoadOSIcon(OSIconName, NULL, FALSE);
        }
        if (Row == 0) {
            Entry->me.BadgeImage = BuiltinIcon(BUILTIN_ICON_VOL_EFI);
        } else {
            Entry->me.BadgeImage = NULL;
        }
        MyFreePool(OSIconName);
        Entry->LoaderPath = NULL;
        Entry->Volume = NULL;
        Entry->LoadOptions = NULL;
        Entry->InitrdPath = NULL;
        Entry->Enabled = TRUE;
    }  
    AddMenuEntry(&MainMenu, (REFIT_MENU_ENTRY *)Entry);
    return Entry;
}  

 
 
static LOADER_ENTRY * AddLoaderEntry(IN OUT CHAR16 *LoaderPath, IN CHAR16 *LoaderTitle,
                                     IN REFIT_VOLUME *Volume, IN BOOLEAN SubScreenReturn) {
    LOADER_ENTRY  *Entry;

    CleanUpPathNameSlashes(LoaderPath);
    Entry = InitializeLoaderEntry(NULL);
    if (Entry != NULL) {
        Entry->DiscoveryType = DISCOVERY_TYPE_AUTO;
        Entry->Title = StrDuplicate((LoaderTitle != NULL) ? LoaderTitle : LoaderPath);
        LOG(1, LOG_LINE_NORMAL, L"Adding loader entry for '%s'", Entry->Title);
        LOG(2, LOG_LINE_NORMAL, L"Loader path is '%s'", LoaderPath);
         
         
         
        if ((Volume->VolName) && (!MyStriCmp(Volume->VolName, L"Recovery HD")))
            Entry->me.Title = PoolPrint(L"Boot %s from %s ",
                                        (LoaderTitle != NULL) ? LoaderTitle : LoaderPath, Volume->VolName);
        else
            Entry->me.Title = PoolPrint(L"Boot %s ", (LoaderTitle != NULL) ? LoaderTitle : LoaderPath);
        Entry->me.Row = 0;
        Entry->me.BadgeImage = Volume->VolBadgeImage;
        if ((LoaderPath != NULL) && (LoaderPath[0] != L'\\')) {
            Entry->LoaderPath = StrDuplicate(L"\\");
        } else {
            Entry->LoaderPath = NULL;
        }
        MergeStrings(&(Entry->LoaderPath), LoaderPath, 0);
        Entry->Volume = Volume;
        SetLoaderDefaults(Entry, LoaderPath, Volume);
        GenerateSubScreen(Entry, Volume, SubScreenReturn);
        AddMenuEntry(&MainMenu, (REFIT_MENU_ENTRY *)Entry);
        LOG(3, LOG_LINE_NORMAL, L"Have successfully created menu entry for '%s'", Entry->Title);
    } else {
        LOG(1, LOG_LINE_NORMAL, L"Unable to initialize loader entry in AddLoaderEntry()!");
    }

    return(Entry);
}  

 
 
 
 
static INTN TimeComp(IN EFI_TIME *Time1, IN EFI_TIME *Time2) {
    INT64 Time1InSeconds, Time2InSeconds;

     
     
    Time1InSeconds = Time1->Second + (Time1->Minute * 60) + (Time1->Hour * 3600) + (Time1->Day * 86400) +
                        (Time1->Month * 2678400) + ((Time1->Year - 1998) * 32140800);
    Time2InSeconds = Time2->Second + (Time2->Minute * 60) + (Time2->Hour * 3600) + (Time2->Day * 86400) +
                        (Time2->Month * 2678400) + ((Time2->Year - 1998) * 32140800);
    if (Time1InSeconds < Time2InSeconds)
        return (-1);
    else if (Time1InSeconds > Time2InSeconds)
        return (1);

    return 0;
}  

 
 
 
 
 
static struct LOADER_LIST * AddLoaderListEntry(struct LOADER_LIST *LoaderList, struct LOADER_LIST *NewEntry) {
    struct LOADER_LIST *LatestEntry, *CurrentEntry, *PrevEntry = NULL;
    BOOLEAN LinuxRescue = FALSE;

    LatestEntry = CurrentEntry = LoaderList;
    if (LoaderList == NULL) {
        LatestEntry = NewEntry;
    } else {
        if (StriSubCmp(L"vmlinuz-0-rescue", NewEntry->FileName))
            LinuxRescue = TRUE;
        while ((CurrentEntry != NULL) && !StriSubCmp(L"vmlinuz-0-rescue", CurrentEntry->FileName) &&
               (LinuxRescue || (TimeComp(&(NewEntry->TimeStamp), &(CurrentEntry->TimeStamp)) < 0))) {
            PrevEntry = CurrentEntry;
            CurrentEntry = CurrentEntry->NextEntry;
        }  
        NewEntry->NextEntry = CurrentEntry;
        if (PrevEntry == NULL) {
            LatestEntry = NewEntry;
        } else {
            PrevEntry->NextEntry = NewEntry;
        }  
    }  
    return (LatestEntry);
}  

 
static VOID CleanUpLoaderList(struct LOADER_LIST *LoaderList) {
    struct LOADER_LIST *Temp;

    while (LoaderList != NULL) {
        Temp = LoaderList;
        LoaderList = LoaderList->NextEntry;
        MyFreePool(Temp->FileName);
        MyFreePool(Temp);
    }  
}  

 
 
 
 
 
/*
 * Directory eligibility depends on both the volume and the path. Match volume
 * exclusions by identifiers available at runtime, and exclude BOOT's own
 * directory on its own device. The same path on another volume may
 * legitimately contain a loader.
 */
static BOOLEAN ShouldScan(REFIT_VOLUME *Volume, CHAR16 *Path) {
    CHAR16   *VolName = NULL, *DontScanDir, *PathCopy = NULL, *VolGuid = NULL;
    UINTN    i = 0;
    BOOLEAN  ScanIt = TRUE;

    VolGuid = GuidAsString(&(Volume->PartGuid));
    if ((IsIn(Volume->FsName, GlobalConfig.DontScanVolumes)) ||
        (IsIn(Volume->PartName, GlobalConfig.DontScanVolumes)) ||
        (IsIn(VolGuid, GlobalConfig.DontScanVolumes))) {
        MyFreePool(VolGuid);
        return FALSE;
    } else {
        MyFreePool(VolGuid);
    }  

    if (MyStriCmp(Path, SelfDirPath) && (Volume->DeviceHandle == SelfVolume->DeviceHandle))
        return FALSE;

     
    PathCopy = StrDuplicate(Path);
    if (SplitVolumeAndFilename(&PathCopy, &VolName)) {
        if (VolName && (!MyStriCmp(VolName, Volume->FsName) &&
            !MyStriCmp(VolName, Volume->PartName))) {
                ScanIt = FALSE;
        }  
    }  
    MyFreePool(PathCopy);
    MyFreePool(VolName);
    VolName = NULL;

     
    while (ScanIt && (DontScanDir = FindCommaDelimited(GlobalConfig.DontScanDirs, i++))) {
        SplitVolumeAndFilename(&DontScanDir, &VolName);
        CleanUpPathNameSlashes(DontScanDir);
        if (VolName != NULL) {
            if (VolumeMatchesDescription(Volume, VolName) && MyStriCmp(DontScanDir, Path))
                ScanIt = FALSE;
        } else {
            if (MyStriCmp(DontScanDir, Path))
                ScanIt = FALSE;
        }
        MyFreePool(DontScanDir);
        MyFreePool(VolName);
        DontScanDir = NULL;
        VolName = NULL;
    }  

    return ScanIt;
}  

 
 
 
 
 
/*
 * Guards against presenting the same fallback boot target more than once.
 * Duplicate suppression is confined to menu discovery and does not change
 * the firmware's registered Boot#### entries.
 */
static BOOLEAN DuplicatesFallback(IN REFIT_VOLUME *Volume, IN CHAR16 *FileName) {
    CHAR8           *FileContents, *FallbackContents;
    EFI_FILE_HANDLE FileHandle, FallbackHandle;
    EFI_FILE_INFO   *FileInfo, *FallbackInfo;
    UINTN           FileSize = 0, FallbackSize = 0;
    EFI_STATUS      Status;
    BOOLEAN         AreIdentical = FALSE;

    if (!FileExists(Volume->RootDir, FileName) || !FileExists(Volume->RootDir, FALLBACK_FULLNAME))
        return FALSE;

    CleanUpPathNameSlashes(FileName);

    if (MyStriCmp(FileName, FALLBACK_FULLNAME))
        return FALSE;  

    Status = refit_call5_wrapper(Volume->RootDir->Open, Volume->RootDir, &FileHandle, FileName, EFI_FILE_MODE_READ, 0);
    if (Status == EFI_SUCCESS) {
        FileInfo = LibFileInfo(FileHandle);
        FileSize = FileInfo->FileSize;
    } else {
        return FALSE;
    }
    MyFreePool(FileInfo);

    Status = refit_call5_wrapper(Volume->RootDir->Open, Volume->RootDir, &FallbackHandle, FALLBACK_FULLNAME, EFI_FILE_MODE_READ, 0);
    if (Status == EFI_SUCCESS) {
        FallbackInfo = LibFileInfo(FallbackHandle);
        FallbackSize = FallbackInfo->FileSize;
    } else {
        refit_call1_wrapper(FileHandle->Close, FileHandle);
        return FALSE;
    }
    MyFreePool(FallbackInfo);
    /*
     * The size test is a cheap filter before reading both candidate files
     * from this volume. The final comparison uses the first read's returned
     * byte count. The code does not separately compare the counts returned by
     * the two reads.
     */
    if (FallbackSize == FileSize) {  
        FileContents = AllocatePool(FileSize);
        FallbackContents = AllocatePool(FallbackSize);
        if (FileContents && FallbackContents) {
            Status = refit_call3_wrapper(FileHandle->Read, FileHandle, &FileSize, FileContents);
            if (Status == EFI_SUCCESS) {
                Status = refit_call3_wrapper(FallbackHandle->Read, FallbackHandle, &FallbackSize, FallbackContents);
            }
            if (Status == EFI_SUCCESS) {
                AreIdentical = (CompareMem(FileContents, FallbackContents, FileSize) == 0);
            }  
        }  
        MyFreePool(FileContents);
        MyFreePool(FallbackContents);
    }  

     
     
    refit_call1_wrapper(FileHandle->Close, FallbackHandle);
    refit_call1_wrapper(FileHandle->Close, FileHandle);
    return AreIdentical;
}  

 
 
 
 
/*
 * The EFI directory listing and an open handle can report different lengths
 * for a link and its target. This scan uses that difference as a heuristic. A
 * link and target with equal reported sizes are indistinguishable here.
 */
static BOOLEAN IsSymbolicLink(REFIT_VOLUME *Volume, CHAR16 *FullName, EFI_FILE_INFO *DirEntry) {
    EFI_FILE_HANDLE FileHandle;
    EFI_FILE_INFO   *FileInfo = NULL;
    EFI_STATUS      Status;
    UINTN           FileSize2 = 0;

    Status = refit_call5_wrapper(Volume->RootDir->Open, Volume->RootDir, &FileHandle, FullName, EFI_FILE_MODE_READ, 0);
    if (Status == EFI_SUCCESS) {
        FileInfo = LibFileInfo(FileHandle);
        if (FileInfo != NULL)
            FileSize2 = FileInfo->FileSize;
    }

    MyFreePool(FileInfo);

    return (DirEntry->FileSize != FileSize2);
}  

 
static VOID LogMenuEntries(UINTN LogLevel) {
    UINTN i;

    if (LogLevel <= GlobalConfig.LogLevel) {
        LOG(LogLevel, LOG_LINE_NORMAL, L"MainMenu currently has %ld entries; they are....", MainMenu.EntryCount);
        for (i = 0; i < MainMenu.EntryCount; i++) {
            if (MainMenu.Entries[i]) {
                 
                 
                LOG(LogLevel, LOG_LINE_NORMAL, L"  --> Menu item %ld is '%s'", i, MainMenu.Entries[i]->Title);
            }
        }
    }
}  

 
 
 
 
 
/*
 * Collect eligible filenames into a sorted list before creating menu entries.
 * With kernel folding enabled, the first qualifying Linux entry in that order
 * anchors the later kernels' submenu. The return value reports a duplicate of
 * the fallback loader found during this scan.
 */
static BOOLEAN ScanLoaderDir(IN REFIT_VOLUME *Volume, IN CHAR16 *Path, IN CHAR16 *Pattern)
{
    EFI_STATUS              Status;
    REFIT_DIR_ITER          DirIter;
    EFI_FILE_INFO           *DirEntry;
    CHAR16                  *Message, *Extension, *FullName;
    struct LOADER_LIST      *LoaderList = NULL, *NewLoader;
    LOADER_ENTRY            *FirstKernel = NULL, *LatestEntry = NULL;
    BOOLEAN                 FoundFallbackDuplicate = FALSE, IsLinux = FALSE, InSelfPath;

    LOG(3, LOG_LINE_NORMAL, L"Beginning to scan directory '%s' for '%s'", Path, Pattern);
    InSelfPath = MyStriCmp(Path, SelfDirPath);
    if ((!SelfDirPath || !Path || (InSelfPath && (Volume->DeviceHandle != SelfVolume->DeviceHandle)) ||
           (!InSelfPath)) && (ShouldScan(Volume, Path))) {
        
       DirIterOpen(Volume->RootDir, Path, &DirIter);
       while (DirIterNext(&DirIter, 2, Pattern, &DirEntry)) {
          Extension = FindExtension(DirEntry->FileName);
          FullName = StrDuplicate(Path);
          MergeStrings(&FullName, DirEntry->FileName, L'\\');
          CleanUpPathNameSlashes(FullName);

          if (!GlobalConfig.FollowSymlinks && IsSymbolicLink(Volume, FullName, DirEntry)) {
              continue;    
          }
          if (DirEntry->FileName[0] == '.' ||
              MyStriCmp(Extension, L".icns") ||
              MyStriCmp(Extension, L".png") ||
              (MyStriCmp(DirEntry->FileName, FALLBACK_BASENAME) && (MyStriCmp(Path, L"EFI\\BOOT"))) ||
              HasSignedCounterpart(Volume, FullName) ||  
              FilenameIn(Volume, Path, DirEntry->FileName, GlobalConfig.DontScanFiles) ||
              !IsValidLoader(Volume->RootDir, FullName)) {
                continue;    
          }

          NewLoader = AllocateZeroPool(sizeof(struct LOADER_LIST));
          if (NewLoader != NULL) {
             NewLoader->FileName = StrDuplicate(FullName);
             NewLoader->TimeStamp = DirEntry->ModificationTime;
             LoaderList = AddLoaderListEntry(LoaderList, NewLoader);
             if (DuplicatesFallback(Volume, FullName))
                FoundFallbackDuplicate = TRUE;
          }  
          if (!IsIn(Path, GlobalConfig.ExtraToolLocations)) {
              MergeStrings(&GlobalConfig.ExtraToolLocations, Path, L',');
              LOG(3, LOG_LINE_NORMAL, L"Adding '%s' to ExtraToolLocations", Path);
          }
          MyFreePool(Extension);
          MyFreePool(FullName);
       }  

       NewLoader = LoaderList;
       while (NewLoader != NULL) {
           IsLinux = IsInSubstring(NewLoader->FileName, GlobalConfig.LinuxPrefixes);
           if ((FirstKernel != NULL) && IsLinux && GlobalConfig.FoldLinuxKernels) {
               AddKernelToSubmenu(FirstKernel, NewLoader->FileName, Volume);
           } else {
               LatestEntry = AddLoaderEntry(NewLoader->FileName, NULL, Volume,
                                            !(IsLinux && GlobalConfig.FoldLinuxKernels));
               if (IsLinux && (FirstKernel == NULL))
                   FirstKernel = LatestEntry;
           }
           NewLoader = NewLoader->NextEntry;
       }  
       if ((FirstKernel != NULL) && IsLinux && GlobalConfig.FoldLinuxKernels)
           AddMenuEntry(FirstKernel->me.SubScreen, &MenuEntryReturn);
       LogMenuEntries(4);

       CleanUpLoaderList(LoaderList);
       Status = DirIterClose(&DirIter);
        
        
        
        
       if ((Status != EFI_NOT_FOUND) && (Status != EFI_INVALID_PARAMETER)) {
          if (Path)
             Message = PoolPrint(L"while scanning the %s directory on %s", Path, Volume->VolName);
          else
             Message = PoolPrint(L"while scanning the root directory on %s", Path, Volume->VolName);
          CheckError(Status, Message);
          MyFreePool(Message);
       }  
    }  

    LOG(3, LOG_LINE_NORMAL, L"Done scanning directory '%s' for '%s'", Path, Pattern);
    return FoundFallbackDuplicate;
}  

 
 
static CHAR16* RuniPXEDiscover(EFI_HANDLE Volume)
{
    EFI_STATUS       Status;
    EFI_DEVICE_PATH  *FilePath;
    EFI_HANDLE       iPXEHandle;
    CHAR16           *boot_info = NULL;
    UINTN            boot_info_size = 0;

    FilePath = FileDevicePath (Volume, IPXE_DISCOVER_NAME);
    Status = refit_call6_wrapper(BS->LoadImage, FALSE, SelfImageHandle, FilePath, NULL, 0, &iPXEHandle);
    if (Status != 0)
        return NULL;

    Status = refit_call3_wrapper(BS->StartImage, iPXEHandle, &boot_info_size, &boot_info);

    return boot_info;
}  

 
 
 
 
static VOID ScanNetboot() {
    CHAR16        *iPXEFileName = IPXE_NAME;
    CHAR16        *Location;
    REFIT_VOLUME  *NetVolume;

    LOG(1, LOG_LINE_NORMAL, L"Scanning for iPXE boot options");
    if (FileExists(SelfVolume->RootDir, IPXE_DISCOVER_NAME) &&
        FileExists(SelfVolume->RootDir, IPXE_NAME) &&
        IsValidLoader(SelfVolume->RootDir, IPXE_DISCOVER_NAME) &&
        IsValidLoader(SelfVolume->RootDir, IPXE_NAME)) {
            Location = RuniPXEDiscover(SelfVolume->DeviceHandle);
            if (Location != NULL && FileExists(SelfVolume->RootDir, iPXEFileName)) {
                NetVolume = AllocatePool(sizeof(REFIT_VOLUME));
                MyCopyMem(NetVolume, SelfVolume, sizeof(REFIT_VOLUME));
                NetVolume->DiskKind = DISK_KIND_NET;
                NetVolume->VolBadgeImage = BuiltinIcon(BUILTIN_ICON_VOL_NET);
                NetVolume->PartName = NetVolume->VolName = NetVolume->FsName = NULL;
                AddLoaderEntry(iPXEFileName, Location, NetVolume, TRUE);
            }  
    }
}  

 
 
 
static BOOLEAN ScanMacOsLoader(REFIT_VOLUME *Volume, CHAR16* FullFileName) {
    BOOLEAN  ScanFallbackLoader = TRUE;
    CHAR16   *VolName = NULL, *PathName = NULL, *FileName = NULL;

    SplitPathName(FullFileName, &VolName, &PathName, &FileName);
    if (FileExists(Volume->RootDir, FullFileName) && !FilenameIn(Volume, PathName, L"boot.efi", GlobalConfig.DontScanFiles)) {
        AddLoaderEntry(FullFileName, L"macOS", Volume, TRUE);
        if (DuplicatesFallback(Volume, FullFileName))
            ScanFallbackLoader = FALSE;
    }  
    MyFreePool(VolName);
    MyFreePool(PathName);
    MyFreePool(FileName);
    return ScanFallbackLoader;
}  

/*
 * Searches EFI directories on each readable volume, with special handling
 * for the standard fallback to avoid duplicate menu representations.
 */
static VOID ScanEfiFiles(REFIT_VOLUME *Volume) {
    EFI_STATUS       Status;
    REFIT_DIR_ITER   EfiDirIter;
    EFI_FILE_INFO    *EfiDirEntry;
    CHAR16           *FileName, *Directory = NULL, *MatchPatterns, *VolName = NULL, *SelfPath, *Temp;
    UINTN            i, Length;
    BOOLEAN          ScanFallbackLoader = TRUE;
    BOOLEAN          FoundBRBackup = FALSE;

    if (Volume && (Volume->RootDir != NULL) && (Volume->VolName != NULL) && (Volume->IsReadable)) {
        LOG(1, LOG_LINE_NORMAL, L"Scanning EFI files on %s",
            Volume->PartName ? Volume->PartName : Volume->VolName);
        MatchPatterns = StrDuplicate(LOADER_MATCH_PATTERNS);
        if (GlobalConfig.ScanAllLinux)
            MergeStrings(&MatchPatterns, GlobalConfig.LinuxMatchPatterns, L',');

         
        if (ShouldScan(Volume, MACOSX_LOADER_DIR)) {
            FileName = StrDuplicate(MACOSX_LOADER_PATH);
            ScanFallbackLoader &= ScanMacOsLoader(Volume, FileName);
            MyFreePool(FileName);
            DirIterOpen(Volume->RootDir, L"\\", &EfiDirIter);
            while (DirIterNext(&EfiDirIter, 1, NULL, &EfiDirEntry)) {
                if (IsGuid(EfiDirEntry->FileName)) {
                    FileName = PoolPrint(L"%s\\%s", EfiDirEntry->FileName, MACOSX_LOADER_PATH);
                    ScanFallbackLoader &= ScanMacOsLoader(Volume, FileName);
                    MyFreePool(FileName);
                    FileName = PoolPrint(L"%s\\%s", EfiDirEntry->FileName, L"boot.efi");
                    if (!StriSubCmp(FileName, GlobalConfig.MacOSRecoveryFiles))
                        MergeStrings(&GlobalConfig.MacOSRecoveryFiles, FileName, L',');
                    MyFreePool(FileName);
                }  
            }  
            Status = DirIterClose(&EfiDirIter);

             
            FileName = StrDuplicate(L"System\\Library\\CoreServices\\xom.efi");
            if (FileExists(Volume->RootDir, FileName) && !FilenameIn(Volume, MACOSX_LOADER_DIR, L"xom.efi",
                                                                     GlobalConfig.DontScanFiles)) {
                AddLoaderEntry(FileName, L"Windows XP (XoM)", Volume, TRUE);
                if (DuplicatesFallback(Volume, FileName))
                    ScanFallbackLoader = FALSE;
            }
            MyFreePool(FileName);
        }  

         
        if (ShouldScan(Volume, L"EFI\\Microsoft\\Boot")) {
            FileName = StrDuplicate(L"EFI\\Microsoft\\Boot\\bkpbootmgfw.efi");
            if (FileExists(Volume->RootDir, FileName) &&  !FilenameIn(Volume, L"EFI\\Microsoft\\Boot", L"bkpbootmgfw.efi",
                GlobalConfig.DontScanFiles)) {
                    AddLoaderEntry(FileName, L"Microsoft EFI boot (Boot Repair backup)", Volume, TRUE);
                    FoundBRBackup = TRUE;
                    if (DuplicatesFallback(Volume, FileName))
                        ScanFallbackLoader = FALSE;
            }
            MyFreePool(FileName);
            FileName = StrDuplicate(L"EFI\\Microsoft\\Boot\\bootmgfw.efi");
            if (FileExists(Volume->RootDir, FileName) &&
                !FilenameIn(Volume, L"EFI\\Microsoft\\Boot", L"bootmgfw.efi", GlobalConfig.DontScanFiles)) {
                    if (FoundBRBackup)
                        AddLoaderEntry(FileName, L"Supposed Microsoft EFI boot (probably GRUB)", Volume, TRUE);
                    else
                        AddLoaderEntry(FileName, L"Microsoft EFI boot", Volume, TRUE);
                    if (DuplicatesFallback(Volume, FileName))
                        ScanFallbackLoader = FALSE;
            }
            MyFreePool(FileName);
        }  

         
        if (ScanLoaderDir(Volume, L"\\", MatchPatterns))
            ScanFallbackLoader = FALSE;

         
        DirIterOpen(Volume->RootDir, L"EFI", &EfiDirIter);
        while (DirIterNext(&EfiDirIter, 1, NULL, &EfiDirEntry)) {
            if (MyStriCmp(EfiDirEntry->FileName, L"tools") || EfiDirEntry->FileName[0] == '.')
                continue;    
            FileName = PoolPrint(L"EFI\\%s", EfiDirEntry->FileName);
            if (ScanLoaderDir(Volume, FileName, MatchPatterns))
                ScanFallbackLoader = FALSE;
            MyFreePool(FileName);
        }  
        Status = DirIterClose(&EfiDirIter);
        if ((Status != EFI_NOT_FOUND) && (Status != EFI_INVALID_PARAMETER)) {
            Temp = PoolPrint(L"while scanning the EFI directory on %s", Volume->VolName);
            CheckError(Status, Temp);
            MyFreePool(Temp);
        }  

         
        i = 0;
        while ((Directory = FindCommaDelimited(GlobalConfig.AlsoScan, i++)) != NULL) {
            if (ShouldScan(Volume, Directory)) {
                SplitVolumeAndFilename(&Directory, &VolName);
                CleanUpPathNameSlashes(Directory);
                Length = StrLen(Directory);
                if ((Length > 0) && ScanLoaderDir(Volume, Directory, MatchPatterns))
                    ScanFallbackLoader = FALSE;
                MyFreePool(VolName);
            }  
            MyFreePool(Directory);
        }  

         
        SelfPath = DevicePathToStr(SelfLoadedImage->FilePath);
        CleanUpPathNameSlashes(SelfPath);
        if ((Volume->DeviceHandle == SelfLoadedImage->DeviceHandle) && DuplicatesFallback(Volume, SelfPath))
            ScanFallbackLoader = FALSE;
        MyFreePool(SelfPath);

         
         
        if (ScanFallbackLoader && FileExists(Volume->RootDir, FALLBACK_FULLNAME) && ShouldScan(Volume, L"EFI\\BOOT") &&
            !FilenameIn(Volume, L"EFI\\BOOT", FALLBACK_BASENAME, GlobalConfig.DontScanFiles)) {
                Temp = StrDuplicate(FALLBACK_FULLNAME);
                AddLoaderEntry(Temp, L"Fallback boot loader", Volume, TRUE);
                MyFreePool(Temp);
        }
        MyFreePool(MatchPatterns);
    } else {
        LOG(1, LOG_LINE_NORMAL, L"Called ScanEfiFiles() on an invalid volume");
    }
}  

 
static VOID ScanInternal(VOID) {
    UINTN                   VolumeIndex;

    LOG(1, LOG_LINE_THIN_SEP, L"Scanning for internal EFI-mode boot loaders");
    for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
        if (Volumes[VolumeIndex]->DiskKind == DISK_KIND_INTERNAL) {
            ScanEfiFiles(Volumes[VolumeIndex]);
        }
    }  
}  

 
static VOID ScanExternal(VOID) {
    UINTN                   VolumeIndex;

    LOG(1, LOG_LINE_THIN_SEP, L"Scanning for external EFI-mode boot loaders");
    for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
        if (Volumes[VolumeIndex]->DiskKind == DISK_KIND_EXTERNAL) {
            ScanEfiFiles(Volumes[VolumeIndex]);
        }
    }  
}  

 
static VOID ScanOptical(VOID) {
    UINTN                   VolumeIndex;

    LOG(1, LOG_LINE_THIN_SEP, L"Scanning for bootable EFI-mode optical discs");
    for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
        if (Volumes[VolumeIndex]->DiskKind == DISK_KIND_OPTICAL) {
            ScanEfiFiles(Volumes[VolumeIndex]);
        }
    }  
}  

 
 
 
 
 
 
 
 
/*
 * Firmware Boot#### entries are a separate discovery source. A firmware
 * entry and a filesystem-discovered image may refer to the same boot target.
 */
static VOID ScanFirmwareDefined(IN UINTN Row, IN CHAR16 *MatchThis, IN EG_IMAGE *Icon) {
    BOOT_ENTRY_LIST *BootEntries, *CurrentEntry;
    BOOLEAN ScanIt = TRUE;
    CHAR16  *OneElement = NULL;
    CHAR16  *DontScanFirmware;
    UINTN   i;

    if (Row == 0)
        LOG(1, LOG_LINE_THIN_SEP, L"Scanning for EFI firmware-defined boot options");
    DontScanFirmware = ReadHiddenTags(L"HiddenFirmware");
    LOG(2, LOG_LINE_NORMAL, L"GlobalConfig.DontScanFirmware = '%s'", GlobalConfig.DontScanFirmware);
    LOG(2, LOG_LINE_NORMAL, L"Firmware hidden tags = '%s'", DontScanFirmware);
    MergeStrings(&DontScanFirmware, GlobalConfig.DontScanFirmware, L',');
    if (Row == 0) {
        LOG(2, LOG_LINE_NORMAL, L"Also not scanning for shells");
        MergeStrings(&DontScanFirmware, L"shell", L',');
    }
    LOG(3, LOG_LINE_NORMAL, L"Merged firmware don't-scan list is '%s'", DontScanFirmware);
    BootEntries = FindBootOrderEntries();
    CurrentEntry = BootEntries;
    while (CurrentEntry != NULL) {
        if (MatchThis) {
            ScanIt = FALSE;
            i = 0;
            while (!ScanIt && (OneElement = FindCommaDelimited(MatchThis, i++))) {
                if (StriSubCmp(OneElement, CurrentEntry->BootEntry.Label) &&
                    !IsInSubstring(CurrentEntry->BootEntry.Label, DontScanFirmware)) {
                        ScanIt = TRUE;
                }
                MyFreePool(OneElement);
            }  
        } else {
            if (IsInSubstring(CurrentEntry->BootEntry.Label, DontScanFirmware)) {
                ScanIt = FALSE;
            }
        }  
        if (ScanIt) {
            LOG(1, LOG_LINE_NORMAL, L"Adding EFI loader entry for '%s'", CurrentEntry->BootEntry.Label);
            AddEfiLoaderEntry(CurrentEntry->BootEntry.DevPath,
                              CurrentEntry->BootEntry.Label,
                              CurrentEntry->BootEntry.BootNum, Row, Icon);
        }
        CurrentEntry = CurrentEntry->NextBootEntry;
        ScanIt = TRUE;  
    }  
    MyFreePool(DontScanFirmware);
    DeleteBootOrderEntries(BootEntries);
}  

 
EG_IMAGE * GetDiskBadge(IN UINTN DiskType) {
    EG_IMAGE * Badge = NULL;

    switch (DiskType) {
        case BBS_HARDDISK:
            Badge = BuiltinIcon(BUILTIN_ICON_VOL_INTERNAL);
            break;
        case BBS_USB:
            Badge = BuiltinIcon(BUILTIN_ICON_VOL_EXTERNAL);
            break;
        case BBS_CDROM:
            Badge = BuiltinIcon(BUILTIN_ICON_VOL_OPTICAL);
            break;
    }  
    return Badge;
}  

 
 
 

static LOADER_ENTRY * AddToolEntry(REFIT_VOLUME *Volume,IN CHAR16 *LoaderPath, IN CHAR16 *LoaderTitle,
                                   IN EG_IMAGE *Image, IN CHAR16 ShortcutLetter, IN BOOLEAN UseGraphicsMode)
{
    LOADER_ENTRY *Entry;
    CHAR16       *TitleStr = NULL;

    Entry = AllocateZeroPool(sizeof(LOADER_ENTRY));

    TitleStr = PoolPrint(L"Start %s", LoaderTitle);
    Entry->me.Title = TitleStr;
    Entry->me.Tag = TAG_TOOL;
    Entry->me.Row = 1;
    Entry->me.ShortcutLetter = ShortcutLetter;
    Entry->me.Image = Image;
    Entry->LoaderPath = (LoaderPath) ? StrDuplicate(LoaderPath) : NULL;
    Entry->Volume = Volume;
    Entry->UseGraphicsMode = UseGraphicsMode;

    AddMenuEntry(&MainMenu, (REFIT_MENU_ENTRY *)Entry);
    return Entry;
}  

 
/*
 * Temporarily merge persisted hidden entries with the configured exclusions
 * for this scan. Dispatch the requested scan classes, then restore the
 * caller's original exclusion lists so rescanning cannot accumulate the same
 * hidden entries.
 */
VOID ScanForBootloaders(BOOLEAN ShowMessage) {
    UINTN    i;
    CHAR8    s;
    BOOLEAN  ScanForLegacy = FALSE;
    EG_PIXEL BGColor = COLOR_LIGHTBLUE;
    CHAR16   *HiddenTags;
    CHAR16   *OrigDontScanFiles, *OrigDontScanVolumes;

    LOG(1, LOG_LINE_SEPARATOR, L"Scanning for boot loaders");
    if (ShowMessage)
        egDisplayMessage(L"Scanning for boot loaders; please wait....", &BGColor, CENTER);

     
    for (i = 0; i < NUM_SCAN_OPTIONS; i++) {
        s = GlobalConfig.ScanFor[i];
        if ((s == 'c') || (s == 'C') || (s == 'h') || (s == 'H') || (s == 'b') || (s == 'B'))
            ScanForLegacy = TRUE;
    }  

     
    if ((GlobalConfig.LegacyType == LEGACY_TYPE_UEFI) && ScanForLegacy && GlobalConfig.DeepLegacyScan) {
        BdsDeleteAllInvalidLegacyBootOptions();
        BdsAddNonExistingLegacyBootOptions();
    }  

     
     
     
     
     
     
     
     
     
    OrigDontScanFiles = StrDuplicate(GlobalConfig.DontScanFiles);
    OrigDontScanVolumes = StrDuplicate(GlobalConfig.DontScanVolumes);

     
    HiddenTags = ReadHiddenTags(L"HiddenTags");
    if ((HiddenTags) && (StrLen(HiddenTags) > 0)) {
        MergeStrings(&GlobalConfig.DontScanFiles, HiddenTags, L',');
    }
    HiddenTags = ReadHiddenTags(L"HiddenLegacy");
    if ((HiddenTags) && (StrLen(HiddenTags) > 0)) {
        MergeStrings(&GlobalConfig.DontScanVolumes, HiddenTags, L',');
    }

     
    for (i = 0; i < NUM_SCAN_OPTIONS; i++) {
        switch(GlobalConfig.ScanFor[i]) {
            case 'c': case 'C':
                ScanLegacyDisc();
                break;
            case 'h': case 'H':
                ScanLegacyInternal();
                break;
            case 'b': case 'B':
                ScanLegacyExternal();
                break;
            case 'm': case 'M':
                ScanUserConfigured(GlobalConfig.ConfigFilename);
                break;
            case 'e': case 'E':
                ScanExternal();
                break;
            case 'i': case 'I':
                ScanInternal();
                break;
            case 'o': case 'O':
                ScanOptical();
                break;
            case 'n': case 'N':
                ScanNetboot();
                break;
            case 'f': case 'F':
                ScanFirmwareDefined(0, NULL, NULL);
                break;
        }  
    }  

     
    MyFreePool(GlobalConfig.DontScanFiles);
    GlobalConfig.DontScanFiles = OrigDontScanFiles;
    MyFreePool(GlobalConfig.DontScanVolumes);
    GlobalConfig.DontScanVolumes = OrigDontScanVolumes;

     
    LOG(2, LOG_LINE_NORMAL, L"Assigning boot shortcut keys");
    for (i = 0; i < MainMenu.EntryCount && MainMenu.Entries[i]->Row == 0 && i < 9; i++)
        MainMenu.Entries[i]->ShortcutDigit = (CHAR16)('1' + i);

     
}  

 
 
static BOOLEAN IsValidTool(IN REFIT_VOLUME *BaseVolume, CHAR16 *PathName) {
    CHAR16 *DontVolName = NULL, *DontPathName = NULL, *DontFileName = NULL, *DontScanThis = NULL;
    CHAR16 *TestVolName = NULL, *TestPathName = NULL, *TestFileName = NULL, *DontScanTools = NULL;
    BOOLEAN retval = TRUE;
    UINTN i = 0;

    LOG(3, LOG_LINE_NORMAL, L"Checking validity of tool '%s' on '%s'", PathName,
        BaseVolume->PartName ? BaseVolume->PartName : BaseVolume->VolName);
    if (FileExists(BaseVolume->RootDir, PathName) && IsValidLoader(BaseVolume->RootDir, PathName)) {
        if (gHiddenTools == NULL) {
            DontScanTools = ReadHiddenTags(L"HiddenTools");
            if (DontScanTools)
                gHiddenTools = StrDuplicate(DontScanTools);
        } else {
            DontScanTools = StrDuplicate(gHiddenTools);
        }

        MergeStrings(&DontScanTools, GlobalConfig.DontScanTools, L',');
        SplitPathName(PathName, &TestVolName, &TestPathName, &TestFileName);
        while (retval && (DontScanThis = FindCommaDelimited(DontScanTools, i++))) {
            SplitPathName(DontScanThis, &DontVolName, &DontPathName, &DontFileName);
            if (MyStriCmp(TestFileName, DontFileName) &&
                ((DontPathName == NULL) || (MyStriCmp(TestPathName, DontPathName))) &&
                ((DontVolName == NULL) || (VolumeMatchesDescription(BaseVolume, DontVolName)))) {
                retval = FALSE;
            }  
            MyFreePool(DontScanThis);
            MyFreePool(DontVolName);
            DontVolName = NULL;
            MyFreePool(DontPathName);
            DontPathName = NULL;
            MyFreePool(DontFileName);
            DontFileName = NULL;
        }  
    } else
        retval = FALSE;
    LOG(4, LOG_LINE_NORMAL, L"About to free multiple variables in IsValidTool()");
    MyFreePool(TestVolName);
    MyFreePool(TestPathName);
    MyFreePool(TestFileName);
    MyFreePool(DontScanTools);
    LOG(4, LOG_LINE_NORMAL, L"Done checking validity of tool '%s' on '%s'", PathName,
        BaseVolume->PartName ? BaseVolume->PartName : BaseVolume->VolName);
    return retval;
}  

 
 
VOID FindTool(CHAR16 *Locations, CHAR16 *Names, CHAR16 *Description, UINTN Icon) {
    UINTN j, k, VolumeIndex;
    CHAR16 *DirName, *FileName, *PathName, *FullDescription, *VolName = NULL;
    CHAR16 *ScannedLocations = NULL;
    BOOLEAN VolMatch;

    LOG(1, LOG_LINE_NORMAL, L"Scanning for tools '%s' in '%s'", Names, Locations);
    for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
        LOG(2, LOG_LINE_NORMAL, L"FindTool: Scanning volume '%s'", Volumes[VolumeIndex]->VolName);
        if (Volumes[VolumeIndex]->RootDir != NULL) {
            j = 0;
            while ((DirName = FindCommaDelimited(Locations, j++)) != NULL) {
                if (!IsIn(DirName, ScannedLocations)) {
                    MergeStrings(&ScannedLocations, DirName, ',');
                    SplitVolumeAndFilename(&DirName, &VolName);
                    VolMatch = ((VolName == NULL) ||
                                (MyStriCmp(VolName, Volumes[VolumeIndex]->FsName)) ||
                                (MyStriCmp(VolName, Volumes[VolumeIndex]->PartName)) ||
                                (MyStriCmp(VolName, Volumes[VolumeIndex]->VolName)));
                    if (VolMatch && (FileExists(Volumes[VolumeIndex]->RootDir, DirName))) {
                        k = 0;
                        while ((FileName = FindCommaDelimited(Names, k++)) != NULL) {
                            PathName = StrDuplicate(DirName);
                            LOG(3, LOG_LINE_NORMAL, L"FindTool: Checking volume '%s' for '%s'",
                                Volumes[VolumeIndex]->VolName, PathName);
                            MergeStrings(&PathName, FileName, MyStriCmp(PathName, L"\\") ? 0 : L'\\');
                            if (IsValidTool(Volumes[VolumeIndex], PathName)) {
                                FullDescription = PoolPrint(L"%s at %s on %s", Description, PathName,
                                                            Volumes[VolumeIndex]->VolName);
                                LOG(1, LOG_LINE_NORMAL, L"Adding tag for '%s' on '%s'", FileName,
                                    Volumes[VolumeIndex]->VolName);
                                AddToolEntry(Volumes[VolumeIndex], PathName, FullDescription,
                                             BuiltinIcon(Icon), 'S', FALSE);
                                MyFreePool(FullDescription);
                            MyFreePool(PathName);
                            MyFreePool(FileName);
                            }  
                        }  
                    }  
                    MyFreePool(VolName);
                    VolName = NULL;
                } else {
                    LOG(3, LOG_LINE_NORMAL, L"Skipping duplicate directory name: '%s'", DirName);
                }
                MyFreePool(DirName);
            }  
            MyFreePool(ScannedLocations);
            ScannedLocations = NULL;
        }  
    }  
}  

 
 
/*
 * Discovers utility EFI applications separately from operating-system boot
 * targets. The menu distinction influences presentation and dispatch but
 * does not bypass the common EFI image-loading boundary.
 */
VOID ScanForTools(VOID) {
    CHAR16 *FileName = NULL, *VolName = NULL, *Description, *AllToolLocations;
    REFIT_MENU_ENTRY *TempMenuEntry;
    UINTN i, j, VolumeIndex;
    UINT64 osind;
    CHAR8 *b = 0;
    UINT32 CsrValue;

    LOG(1, LOG_LINE_SEPARATOR, L"Scanning for tools");
    if (!IsIn(GlobalConfig.ExtraToolLocations, SelfDirPath))
        MergeStrings(&GlobalConfig.ExtraToolLocations, SelfDirPath, L',');

    AllToolLocations = StrDuplicate(GlobalConfig.ToolLocations);
    MergeStrings(&AllToolLocations, GlobalConfig.ExtraToolLocations, L',');
    for (i = 0; i < NUM_TOOLS; i++) {
        switch(GlobalConfig.ShowTools[i]) {
             
            case TAG_SHUTDOWN:
                TempMenuEntry = CopyMenuEntry(&MenuEntryShutdown);
                TempMenuEntry->Image = BuiltinIcon(BUILTIN_ICON_FUNC_SHUTDOWN);
                AddMenuEntry(&MainMenu, TempMenuEntry);
                break;

            case TAG_REBOOT:
                TempMenuEntry = CopyMenuEntry(&MenuEntryReset);
                TempMenuEntry->Image = BuiltinIcon(BUILTIN_ICON_FUNC_RESET);
                AddMenuEntry(&MainMenu, TempMenuEntry);
                break;

            case TAG_ABOUT:
                TempMenuEntry = CopyMenuEntry(&MenuEntryAbout);
                TempMenuEntry->Image = BuiltinIcon(BUILTIN_ICON_FUNC_ABOUT);
                AddMenuEntry(&MainMenu, TempMenuEntry);
                break;

            case TAG_EXIT:
                TempMenuEntry = CopyMenuEntry(&MenuEntryExit);
                TempMenuEntry->Image = BuiltinIcon(BUILTIN_ICON_FUNC_EXIT);
                AddMenuEntry(&MainMenu, TempMenuEntry);
                break;

            case TAG_HIDDEN:
                if (GlobalConfig.HiddenTags) {
                    TempMenuEntry = CopyMenuEntry(&MenuEntryManageHidden);
                    TempMenuEntry->Image = BuiltinIcon(BUILTIN_ICON_FUNC_HIDDEN);
                    AddMenuEntry(&MainMenu, TempMenuEntry);
                }
                break;

            case TAG_FIRMWARE:
                if (EfivarGetRaw(&GlobalGuid, L"OsIndicationsSupported", &b, &j) == EFI_SUCCESS) {
                    osind = (UINT64)*b;
                    if (osind & EFI_OS_INDICATIONS_BOOT_TO_FW_UI) {
                        TempMenuEntry = CopyMenuEntry(&MenuEntryFirmware);
                        TempMenuEntry->Image = BuiltinIcon(BUILTIN_ICON_FUNC_FIRMWARE);
                        AddMenuEntry(&MainMenu, TempMenuEntry);
                    } else {
                        LOG(1, LOG_LINE_NORMAL, L"showtools includes firmware, but EFI lacks support");
                    }  
                    MyFreePool(b);
                } else {
                    LOG(1, LOG_LINE_NORMAL, L"showtools includes firmware, but OsIndicationsSupported is missing");
                }
                break;

            case TAG_SHELL:
                FindTool(AllToolLocations, SHELL_NAMES, L"EFI shell", BUILTIN_ICON_TOOL_SHELL);
                ScanFirmwareDefined(1, L"Shell", BuiltinIcon(BUILTIN_ICON_TOOL_SHELL));
                break;

            case TAG_GPTSYNC:
                FindTool(AllToolLocations, GPTSYNC_NAMES, L"Hybrid MBR tool", BUILTIN_ICON_TOOL_PART);
                break;

            case TAG_GDISK:
                FindTool(AllToolLocations, GDISK_NAMES, L"Disk partitioning tool", BUILTIN_ICON_TOOL_PART);
                break;

            case TAG_NETBOOT:
                FindTool(AllToolLocations, NETBOOT_NAMES, L"Netboot", BUILTIN_ICON_TOOL_NETBOOT);
                break;

            case TAG_MOK_TOOL:
                FindTool(AllToolLocations, MOK_NAMES, L"MOK utility", BUILTIN_ICON_TOOL_MOK_TOOL);
                break;

            case TAG_FWUPDATE_TOOL:
                FindTool(AllToolLocations, FWUPDATE_NAMES, L"firmware update utility", BUILTIN_ICON_TOOL_FWUPDATE);
                break;

            case TAG_MEMTEST:
                FindTool(AllToolLocations, MEMTEST_NAMES, L"Memory test utility", BUILTIN_ICON_TOOL_MEMTEST);
                break;

            case TAG_APPLE_RECOVERY:
                for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
                    j = 0;
                    while ((FileName = FindCommaDelimited(GlobalConfig.MacOSRecoveryFiles, j++)) != NULL) {
                        if ((Volumes[VolumeIndex]->RootDir != NULL)) {
                            if ((IsValidTool(Volumes[VolumeIndex], FileName))) {
                                Description = PoolPrint(L"Apple Recovery on %s", Volumes[VolumeIndex]->VolName);
                                LOG(1, LOG_LINE_NORMAL, L"Adding Apple Recovery tag for '%s' on '%s'", FileName,
                                    Volumes[VolumeIndex]->VolName);
                                AddToolEntry(Volumes[VolumeIndex], FileName, Description,
                                                BuiltinIcon(BUILTIN_ICON_TOOL_APPLE_RESCUE), 'R', TRUE);
                                MyFreePool(Description);
                            }  
                        }  
                    }  
                }  
                break;

            case TAG_WINDOWS_RECOVERY:
                j = 0;
                while ((FileName = FindCommaDelimited(GlobalConfig.WindowsRecoveryFiles, j++)) != NULL) {
                    SplitVolumeAndFilename(&FileName, &VolName);
                    for (VolumeIndex = 0; VolumeIndex < VolumesCount; VolumeIndex++) {
                        if ((Volumes[VolumeIndex]->RootDir != NULL) &&
                            (IsValidTool(Volumes[VolumeIndex], FileName)) &&
                            ((VolName == NULL) || MyStriCmp(VolName, Volumes[VolumeIndex]->VolName))) {
                                Description = PoolPrint(L"Microsoft Recovery on %s", Volumes[VolumeIndex]->VolName);
                                LOG(1, LOG_LINE_NORMAL,
                                    L"Adding Windows Recovery tag for '%s' on '%s'",
                                    FileName, Volumes[VolumeIndex]->VolName);
                                AddToolEntry(Volumes[VolumeIndex], FileName, Description,
                                             BuiltinIcon(BUILTIN_ICON_TOOL_WINDOWS_RESCUE), 'R', TRUE);
                                MyFreePool(Description);
                        }  
                    }  
                    MyFreePool(FileName);
                    MyFreePool(VolName);
                    VolName = NULL;
                }  
                FileName = NULL;
                break;

            case TAG_CSR_ROTATE:
                if ((GetCsrStatus(&CsrValue) == EFI_SUCCESS) && (GlobalConfig.CsrValues)) {
                    TempMenuEntry = CopyMenuEntry(&MenuEntryRotateCsr);
                    TempMenuEntry->Image = BuiltinIcon(BUILTIN_ICON_FUNC_CSR_ROTATE);
                    AddMenuEntry(&MainMenu, TempMenuEntry);
                }  
                break;

            case TAG_BOOTORDER:
                TempMenuEntry = CopyMenuEntry(&MenuEntryBootorder);
                TempMenuEntry->Image = BuiltinIcon(BUILTIN_ICON_FUNC_BOOTORDER);
                AddMenuEntry(&MainMenu, TempMenuEntry);
                break;

        }  
    }  
    MyFreePool(AllToolLocations);
    LOG(2, LOG_LINE_NORMAL, L"Done scanning for tools");
}  
