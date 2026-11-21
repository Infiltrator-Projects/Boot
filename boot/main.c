 


































 






 














#include "global.h"
#include "config.h"
#include "screen.h"
#include "launch_legacy.h"
#include "lib.h"
#include "icns.h"
#include "install.h"
#include "menu.h"
#include "mok.h"
#include "apple.h"
#include "mystrings.h"
#include "security_policy.h"
#include "driver_support.h"
#include "launch_efi.h"
#include "scan.h"
#include "log.h"
#include "../include/refit_call_wrapper.h"
#include "../include/version.h"
#include "../libeg/efiConsoleControl.h"
#include "../libeg/efiUgaDraw.h"

#ifndef __MAKEWITH_GNUEFI
#define LibLocateProtocol EfiLibLocateProtocol
#endif

 
 

REFIT_MENU_ENTRY MenuEntryReturn   = { L"Return to Main Menu", TAG_RETURN, 1, 0, 0, NULL, NULL, NULL };

REFIT_MENU_SCREEN MainMenu       = { L"Main Menu", NULL, 0, NULL, 0, NULL, 0, L"Automatic boot",
                                     L"Use arrow keys to move cursor; Enter to boot;",
                                     L"Insert, Tab, or F2 for more options; Esc or Backspace to refresh" };
static REFIT_MENU_SCREEN AboutMenu      = { L"About", NULL, 0, NULL, 0, NULL, 0, NULL, L"Press Enter to return to main menu", L"" };

REFIT_CONFIG GlobalConfig = {   FALSE,
                                TRUE,
                                FALSE,
                                FALSE,
                                TRUE,
                                FALSE,
                                FALSE,
                                TRUE,
                                TRUE,
                                FALSE,
                                FALSE,
                                FALSE,
                                FALSE,
#ifdef EFIAARCH64
                                TRUE,
#else
                                FALSE,
#endif
                                0,
                                0,
                                0,
                                DONT_CHANGE_TEXT_MODE,
                                20,
                                0,
                                0,
                                GRAPHICS_FOR_OSX,
                                LEGACY_TYPE_MAC,
                                0,
                                0,
                                4,
                                { DEFAULT_BIG_ICON_SIZE / 4,
                                                  DEFAULT_SMALL_ICON_SIZE,
                                                  DEFAULT_BIG_ICON_SIZE,
                                                  DEFAULT_MOUSE_SIZE },
                                BANNER_NOSCALE,
                                0,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                CONFIG_FILE_NAME,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                NULL,
                                { TAG_SHELL, TAG_MEMTEST, TAG_GDISK, TAG_APPLE_RECOVERY, TAG_WINDOWS_RECOVERY,
                                                  TAG_MOK_TOOL, TAG_ABOUT, TAG_HIDDEN, TAG_SHUTDOWN, TAG_REBOOT, TAG_FIRMWARE,
                                                  TAG_FWUPDATE_TOOL, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
                            };

CHAR16 *gHiddenTools = NULL;

EFI_GUID BootGuid = BOOT_GUID_VALUE;

 
 
 

VOID AboutBOOT(VOID)
{
    CHAR16     *FirmwareVendor;
    CHAR16     *TempStr;
    UINT32     CsrStatus;

    LOG(1, LOG_LINE_SEPARATOR, L"Displaying About/Info screen");
    if (AboutMenu.EntryCount == 0) {
        AboutMenu.TitleImage = BuiltinIcon(BUILTIN_ICON_FUNC_ABOUT);
        AddMenuInfoLine(&AboutMenu, PoolPrint(L"BOOT Version %s", BOOT_VERSION));
        AddMenuInfoLine(&AboutMenu, L"");
        AddMenuInfoLine(&AboutMenu, L"Copyright (c) 2006-2010 Christoph Pfisterer");
        AddMenuInfoLine(&AboutMenu, L"Copyright (c) 2012-2024 Roderick W. Smith");
        AddMenuInfoLine(&AboutMenu, L"Portions Copyright (c) Intel Corporation and others");
        AddMenuInfoLine(&AboutMenu, L"Distributed under the terms of the GNU GPLv3 license");
        AddMenuInfoLine(&AboutMenu, L"");
        AddMenuInfoLine(&AboutMenu, L"Running on:");
        FirmwareVendor = StrDuplicate(ST->FirmwareVendor);
        LimitStringLength(FirmwareVendor, MAX_LINE_LENGTH);  
        AddMenuInfoLine(&AboutMenu, PoolPrint(L" Firmware: %s %d.%02d", FirmwareVendor,
                                              ST->FirmwareRevision >> 16,
                                              ST->FirmwareRevision & ((1 << 16) - 1)));
        AddMenuInfoLine(&AboutMenu, PoolPrint(L" EFI Revision %d.%02d", ST->Hdr.Revision >> 16, ST->Hdr.Revision & ((1 << 16) - 1)));
#if defined(EFI32)
        AddMenuInfoLine(&AboutMenu, PoolPrint(L" Platform: x86 (32 bit); Secure Boot %s %s",
                                              secure_mode() ? L"active" : L"inactive",
                                              ShimLoaded() ? L"(via Shim)" : L"(direct)"));
#elif defined(EFIX64)
        AddMenuInfoLine(&AboutMenu, PoolPrint(L" Platform: x86_64 (64 bit); Secure Boot %s %s",
                                              secure_mode() ? L"active" : L"inactive",
                                              ShimLoaded() ? L"(via Shim)" : L"(direct)"));
#elif defined(EFIAARCH64)
        AddMenuInfoLine(&AboutMenu, PoolPrint(L" Platform: ARM (64 bit); Secure Boot %s %s",
                                              secure_mode() ? L"active" : L"inactive",
                                              ShimLoaded() ? L"(via Shim)" : L"(direct)"));
#else
        AddMenuInfoLine(&AboutMenu, L" Platform: unknown");
#endif
        if (GetCsrStatus(&CsrStatus) == EFI_SUCCESS) {
            RecordgCsrStatus(CsrStatus, FALSE);
            AddMenuInfoLine(&AboutMenu, gCsrStatus);
        }
        TempStr = egScreenDescription();
        AddMenuInfoLine(&AboutMenu, PoolPrint(L" Screen Output: %s", TempStr));
        MyFreePool(TempStr);
        AddMenuInfoLine(&AboutMenu, L"");
#if defined(__MAKEWITH_GNUEFI)
        AddMenuInfoLine(&AboutMenu, L"Built with GNU-EFI");
#else
        AddMenuInfoLine(&AboutMenu, L"Built with TianoCore EDK2");
#endif
        AddMenuInfoLine(&AboutMenu, L"");
        AddMenuInfoLine(&AboutMenu, L"For more information, see the BOOT Web site:");
        AddMenuInfoLine(&AboutMenu, L"https://github.com/Infiltrator-Projects/Boot");
        AddMenuEntry(&AboutMenu, &MenuEntryReturn);
        MyFreePool(FirmwareVendor);
    }

    RunMenu(&AboutMenu, NULL);
}  

 
 
VOID StoreLoaderName(IN CHAR16 *Name) {

    if (Name) {
        EfivarSetRaw(&BootGuid, L"PreviousBoot", (CHAR8*) Name, StrLen(Name) * 2 + 2, TRUE);
    }  
}  

 
VOID RescanAll(BOOLEAN DisplayMessage, BOOLEAN Reconnect) {
    LOG(1, LOG_LINE_NORMAL, L"Re-scanning all boot loaders");
    FreeList((VOID ***) &(MainMenu.Entries), &MainMenu.EntryCount);
    MainMenu.Entries = NULL;
    MainMenu.EntryCount = 0;
     
     
    if (Reconnect) {
        ConnectAllDriversToAllControllers();
        ScanVolumes();
    }
    ReadConfig(GlobalConfig.ConfigFilename);
    SetVolumeIcons();
    ScanForBootloaders(DisplayMessage);
    ScanForTools();
}  

#ifdef __MAKEWITH_TIANO

 
static VOID InitializeLib(IN EFI_HANDLE ImageHandle, IN EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS Status;

    gST            = SystemTable;
     
    gBS            = SystemTable->BootServices;
     
    gRT = SystemTable->RuntimeServices;  
    Status = EfiGetSystemConfigurationTable (&gEfiDxeServicesTableGuid, (VOID **) &gDS);
    if (EFI_ERROR(Status)) {
         
         
        gDS = 0;
    }
}

#endif

 
 
static BOOLEAN SecureBootSetup(VOID) {
    EFI_STATUS Status;
    BOOLEAN    Success = FALSE;

    LOG(1, LOG_LINE_NORMAL, L"Setting up Secure Boot (if applicable)");
    if (secure_mode() && ShimLoaded()) {
        LOG(2, LOG_LINE_NORMAL, L"Secure boot mode detected with loaded Shim; adding MOK extensions");
        Status = security_policy_install();
        if (Status == EFI_SUCCESS) {
            Success = TRUE;
        } else {
            Print(L"Failed to install MOK Secure Boot extensions");
            PauseForKey();
        }
    } else {
        LOG(2, LOG_LINE_NORMAL, L"Secure boot disabled; doing nothing");
    }
    return Success;
}  

 
 
static BOOLEAN SecureBootUninstall(VOID) {
    EFI_STATUS Status;
    BOOLEAN    Success = TRUE;

    if (secure_mode()) {
        Status = security_policy_uninstall();
        if (Status != EFI_SUCCESS) {
            Success = FALSE;
            BeginTextScreen(L"Secure Boot Policy Failure");
            Print(L"Failed to uninstall MOK Secure Boot extensions; forcing a reboot.");
            PauseForKey();
            refit_call4_wrapper(RT->ResetSystem, EfiResetCold, EFI_SUCCESS, 0, NULL);
        }
    }
    return Success;
}  

 
 
 
 
static VOID SetConfigFilename(EFI_HANDLE ImageHandle) {
    EFI_LOADED_IMAGE *Info;
    CHAR16 *Options, *FileName, *SubString;
    EFI_STATUS Status;

    Status = refit_call3_wrapper(BS->HandleProtocol, ImageHandle, &LoadedImageProtocol, (VOID **) &Info);
    if ((Status == EFI_SUCCESS) && (Info->LoadOptionsSize > 0)) {
        Options = (CHAR16 *) Info->LoadOptions;
        SubString = MyStrStr(Options, L" -c ");
        if (SubString) {
            FileName = StrDuplicate(&SubString[4]);
            if (FileName) {
                LimitStringLength(FileName, 256);
            }

            if (FileExists(SelfDir, FileName)) {
                GlobalConfig.ConfigFilename = FileName;
            } else {
                Print(L"Specified configuration file (%s) doesn't exist; using\n'boot.conf' default\n", FileName);
                MyFreePool(FileName);
            }  
        }  
    }  
}  

 
 
 
static VOID AdjustDefaultSelection() {
    UINTN i = 0, j;
    CHAR16 *Element = NULL, *NewCommaDelimited = NULL, *PreviousBoot = NULL;
    EFI_STATUS Status;

    LOG(1, LOG_LINE_NORMAL, L"Adjusting default_selection with PreviousBoot values");
    while ((Element = FindCommaDelimited(GlobalConfig.DefaultSelection, i++)) != NULL) {
        if (MyStriCmp(Element, L"+")) {
            Status = EfivarGetRaw(&BootGuid, L"PreviousBoot", (CHAR8 **) &PreviousBoot, &j);
            if (Status == EFI_SUCCESS) {
                MyFreePool(Element);
                Element = PreviousBoot;
            } else {
                Element = NULL;
            }
        }  
        if (Element && StrLen(Element)) {
            MergeStrings(&NewCommaDelimited, Element, L',');
        }  
        MyFreePool(Element);
    }  
    MyFreePool(GlobalConfig.DefaultSelection);
    GlobalConfig.DefaultSelection = NewCommaDelimited;
}  

 
VOID LogBasicInfo(VOID) {
    EFI_STATUS Status;
    UINT64     MaximumVariableStorageSize;
    UINT64     RemainingVariableStorageSize;
    UINT64     MaximumVariableSize;
    UINTN      EfiMajorVersion = ST->Hdr.Revision >> 16;
    CHAR16     *TempStr;
    EFI_GUID   ConsoleControlProtocolGuid = EFI_CONSOLE_CONTROL_PROTOCOL_GUID;
    EFI_GUID   UgaDrawProtocolGuid = EFI_UGA_DRAW_PROTOCOL_GUID;
    EFI_GUID   GraphicsOutputProtocolGuid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;

    LOG(1, LOG_LINE_SEPARATOR, L"System information");
#if defined(__MAKEWITH_GNUEFI)
    LOG(1, LOG_LINE_NORMAL, L"BOOT %s built with GNU-EFI", BOOT_VERSION);
#else
    LOG(1, LOG_LINE_NORMAL, L"BOOT %s built with TianoCore EDK2", BOOT_VERSION);
#endif
    TempStr = GuidAsString(&(SelfVolume->PartGuid));
    LOG(1, LOG_LINE_NORMAL, L"BOOT boot partition GUID: %s", TempStr);
    MyFreePool(TempStr);
#if defined(EFI32)
    LOG(1, LOG_LINE_NORMAL, L"Platform: x86/IA32/i386 (32-bit)");
#elif defined(EFIX64)
    LOG(1, LOG_LINE_NORMAL, L"Platform: x86-64/X64/AMD64 (64-bit)");
#elif defined(EFIAARCH64)
    LOG(1, LOG_LINE_NORMAL, L"Platform: ARM64/AARCH64 (64-bit)");
#else
    LOG(1, LOG_LINE_NORMAL, L"Platform: unknown");
#endif
    LOG(1, LOG_LINE_NORMAL, L"Log level is %d", GlobalConfig.LogLevel);
    LOG(1, LOG_LINE_NORMAL, L"Menu timeout is %d", GlobalConfig.Timeout);
    LOG(1, LOG_LINE_NORMAL, L"Firmware: %s %d.%02d", ST->FirmwareVendor,
        ST->FirmwareRevision >> 16, ST->FirmwareRevision & ((1 << 16) - 1));
    LOG(1, LOG_LINE_NORMAL, L"EFI Revision %d.%02d", EfiMajorVersion,
        ST->Hdr.Revision & ((1 << 16) - 1));
    LOG(1, LOG_LINE_NORMAL, L"Secure Boot %s", secure_mode() ? L"active" : L"inactive");
    LOG(1, LOG_LINE_NORMAL, L"Shim is%s available", ShimLoaded() ? L"" : L" not");
    switch (GlobalConfig.LegacyType) {
        case LEGACY_TYPE_MAC:
            TempStr = L"CSM type: Mac";
            break;
        case LEGACY_TYPE_UEFI:
            TempStr = L"CSM type: UEFI";
            break;
        case LEGACY_TYPE_NONE:
            TempStr = L"CSM is not available";
            break;
        default:  
            TempStr = L"CSM type: unknown";
            break;
    }
    LOG(1, LOG_LINE_NORMAL, TempStr);
    if (EfiMajorVersion > 1) {  
        LOG(3, LOG_LINE_NORMAL, L"Trying to get variable info....");
        Status = refit_call4_wrapper(RT->QueryVariableInfo, EFI_VARIABLE_NON_VOLATILE,
                                     &MaximumVariableStorageSize, &RemainingVariableStorageSize,
                                     &MaximumVariableSize);
        if (EFI_ERROR(Status)) {
            LOG(1, LOG_LINE_NORMAL, L"Error %d; Unable to retrieve EFI variable capacity", Status);
        } else {
            LOG(1, LOG_LINE_NORMAL, L"EFI non-volatile storage:");
            LOG(1, LOG_LINE_NORMAL, L"   Total storage: %ld", MaximumVariableStorageSize);
            LOG(1, LOG_LINE_NORMAL, L"   Remaining available: %ld", RemainingVariableStorageSize);
            LOG(1, LOG_LINE_NORMAL, L"   Maximum variable size: %ld", MaximumVariableSize);
        }
    } else {
        LOG(1, LOG_LINE_NORMAL, L"EFI 1.x; EFI non-volatile storage information is unavailable");
    }

     
     
    Status = LibLocateProtocol(&ConsoleControlProtocolGuid, (VOID **) &TempStr);
    LOG(1, LOG_LINE_NORMAL, L"System does%s support text mode",
        EFI_ERROR(Status) ? L" not" : L"");

    Status = LibLocateProtocol(&UgaDrawProtocolGuid, (VOID **) &TempStr);
    LOG(1, LOG_LINE_NORMAL, L"System does%s support UGA Draw graphics mode",
        EFI_ERROR(Status) ? L" not" : L"");

    Status = LibLocateProtocol(&GraphicsOutputProtocolGuid, (VOID **) &TempStr);
    LOG(1, LOG_LINE_NORMAL, L"System does%s support GOP graphics mode",
        EFI_ERROR(Status) ? L" not" : L"");

#ifdef __MAKEWITH_TIANO
    if (gDS == 0) {
        LOG(1, LOG_LINE_NORMAL, L"WARNING: EfiGetSystemConfigurationTable() returned error status %lu!", Status);
        LOG(1, LOG_LINE_NORMAL, L"         Some functionality will be impaired!");
    }
#endif
}  

 
 
 
EFI_STATUS
EFIAPI
efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    EFI_STATUS         Status;
    BOOLEAN            MainLoopRunning = TRUE;
    BOOLEAN            MokProtocol;
    REFIT_MENU_ENTRY   *ChosenEntry;
    UINTN              MenuExit = MENU_EXIT_ENTER, i;
    CHAR16             *SelectionName = NULL;
    EG_PIXEL           BGColor = COLOR_LIGHTBLUE;

     
    InitializeLib(ImageHandle, SystemTable);
    Status = InitRefitLib(ImageHandle);
    if (EFI_ERROR(Status))
        return Status;

     
    MyCopyMem(GlobalConfig.ScanFor, "ieom       ", NUM_SCAN_OPTIONS);
    FindLegacyBootType();
    if (GlobalConfig.LegacyType == LEGACY_TYPE_MAC)
       MyCopyMem(GlobalConfig.ScanFor, "ihebocm    ", NUM_SCAN_OPTIONS);
    SetConfigFilename(ImageHandle);

     
     
     
     
    ScanVolumes();
    ReadConfig(GlobalConfig.ConfigFilename);
    if (GlobalConfig.LogLevel > 0) {
        StartLogging(FALSE);
        LogBasicInfo();
    }
    LOG(3, LOG_LINE_NORMAL, L"GlobalConfig.DontScanFiles is '%s'", GlobalConfig.DontScanFiles);
    MokProtocol = SecureBootSetup();
    if (LoadDrivers())
        ScanVolumes();

    LOG(1, LOG_LINE_SEPARATOR, L"Initializing basic features");
    AdjustDefaultSelection();

    if (GlobalConfig.SpoofOSXVersion && GlobalConfig.SpoofOSXVersion[0] != L'\0')
        SetAppleOSInfo();

    InitScreen();
    WarnIfLegacyProblems();
    MainMenu.TimeoutSeconds = GlobalConfig.Timeout;

     
    LOG(1, LOG_LINE_NORMAL, L"Setting watchdog timer");
    refit_call4_wrapper(BS->SetWatchdogTimer, 0x0000, 0x0000, 0x0000, NULL);

     
    SetupScreen();
    SetVolumeIcons();
    ScanForBootloaders(FALSE);
    ScanForTools();

     
     
    BltClearScreen(TRUE);
    pdInitialize();

    if (GlobalConfig.ScanDelay > 0) {
       if (GlobalConfig.ScanDelay > 1) {
          LOG(1, LOG_LINE_NORMAL, L"Pausing before re-scan");
          egDisplayMessage(L"Pausing before disk scan; please wait....", &BGColor, CENTER);
       }
       for (i = 0; i < GlobalConfig.ScanDelay; i++)
          refit_call1_wrapper(BS->Stall, 1000000);
       RescanAll(GlobalConfig.ScanDelay > 1, TRUE);
       BltClearScreen(TRUE);
    }  

    if (GlobalConfig.DefaultSelection)
       SelectionName = StrDuplicate(GlobalConfig.DefaultSelection);
    if (GlobalConfig.ShutdownAfterTimeout)
        MainMenu.TimeoutText = L"Shutdown";

    LOG(1, LOG_LINE_SEPARATOR, L"Entering main loop");
    while (MainLoopRunning) {
        MenuExit = RunMainMenu(&MainMenu, &SelectionName, &ChosenEntry);

         
        if (MenuExit == MENU_EXIT_ESCAPE) {
            MenuExit = 0;
            RescanAll(TRUE, TRUE);
            continue;
        }

        if ((MenuExit == MENU_EXIT_TIMEOUT) && GlobalConfig.ShutdownAfterTimeout) {
            ChosenEntry->Tag = TAG_SHUTDOWN;
        }

        switch (ChosenEntry->Tag) {

            case TAG_REBOOT:     
                TerminateScreen();
                LOG(1, LOG_LINE_SEPARATOR, L"Rebooting system");
                refit_call4_wrapper(RT->ResetSystem, EfiResetCold, EFI_SUCCESS, 0, NULL);
                LOG(1, LOG_LINE_NORMAL, L"Reboot FAILED!");
                MainLoopRunning = FALSE;    
                break;

            case TAG_SHUTDOWN:  
                TerminateScreen();
                LOG(1, LOG_LINE_SEPARATOR, L"Shutting down system");
                refit_call4_wrapper(RT->ResetSystem, EfiResetShutdown, EFI_SUCCESS, 0, NULL);
                LOG(1, LOG_LINE_NORMAL, L"Shutdown FAILED!");
                MainLoopRunning = FALSE;    
                break;

            case TAG_ABOUT:     
                AboutBOOT();
                break;

            case TAG_LOADER:    
                StartLoader((LOADER_ENTRY *)ChosenEntry, SelectionName);
                break;

            case TAG_LEGACY:    
                StartLegacy((LEGACY_ENTRY *)ChosenEntry, SelectionName);
                break;

            case TAG_LEGACY_UEFI:  
                StartLegacyUEFI((LEGACY_ENTRY *)ChosenEntry, SelectionName);
                break;

            case TAG_FIRMWARE_LOADER:  
                RebootIntoLoader((LOADER_ENTRY *)ChosenEntry);
                break;

            case TAG_TOOL:      
                StartTool((LOADER_ENTRY *)ChosenEntry);
                break;

            case TAG_HIDDEN:   
                ManageHiddenTags();
                break;

            case TAG_EXIT:     
                if ((MokProtocol) && !SecureBootUninstall()) {
                   MainLoopRunning = FALSE;    
                } else {
                   BeginTextScreen(L" ");
                   return EFI_SUCCESS;
                }
                break;

            case TAG_FIRMWARE:  
                RebootIntoFirmware();
                break;

            case TAG_CSR_ROTATE:
                RotateCsrValue();
                break;
            case TAG_BOOTORDER:
                ManageBootorder();
                break;

        }  
    }  
    MyFreePool(SelectionName);

     
     
    LOG(1, LOG_LINE_SEPARATOR, L"Main loop has exited, but it should not have!");
    UninitRefitLib();
    refit_call4_wrapper(RT->ResetSystem, EfiResetCold, EFI_SUCCESS, 0, NULL);
    ReinitRefitLib();
    LOG(1, LOG_LINE_SEPARATOR, L"Shutdown after main loop exit has FAILED!");
    StopLogging();
    EndlessIdleLoop();

    return EFI_SUCCESS;
}  
