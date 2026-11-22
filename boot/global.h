/*
 * Shared records used from volume discovery through menu dispatch.
 * REFIT_VOLUME ties disk and partition identities to firmware handles.
 * Loader and legacy records embed REFIT_MENU_ENTRY as their first member.
 * RunMainMenu returns that member. Its tag tells efi_main how to recover
 * the enclosing record and which operation the selected entry requests.
 */

#ifndef __GLOBAL_H_
#define __GLOBAL_H_

#ifdef __MAKEWITH_GNUEFI
#include <efi.h>
#include <efilib.h>
#else
#include "../include/tiano_includes.h"
#endif
#include "../EfiLib/GenericBdsLib.h"

#include "../libeg/libeg.h"
/*
 * RunMainMenu returns a pointer to a base menu entry. efi_main uses its tag to
 * select the enclosing structure and the dispatch method. In particular,
 * TAG_LOADER carries a filesystem path, while TAG_FIRMWARE_LOADER selects a
 * 16-bit firmware boot option.
 */
#define TAG_ABOUT            (1)
#define TAG_REBOOT           (2)
#define TAG_SHUTDOWN         (3)
#define TAG_TOOL             (4)
#define TAG_LOADER           (5)
#define TAG_LEGACY           (6)
#define TAG_FIRMWARE_LOADER  (7)
#define TAG_EXIT             (8)
#define TAG_SHELL            (9)
#define TAG_GPTSYNC          (10)
#define TAG_LEGACY_UEFI      (11)
#define TAG_APPLE_RECOVERY   (12)
#define TAG_WINDOWS_RECOVERY (13)
#define TAG_MOK_TOOL         (14)
#define TAG_FIRMWARE         (15)
#define TAG_MEMTEST          (16)
#define TAG_GDISK            (17)
#define TAG_NETBOOT          (18)
#define TAG_CSR_ROTATE       (19)
#define TAG_FWUPDATE_TOOL    (20)
#define TAG_HIDDEN           (21)
#define TAG_BOOTORDER        (23)
#define NUM_TOOLS            (24)
/*
 * ScanFor is an ordered, fixed-width array of dispatch letters.
 * ScanForBootloaders iterates NUM_SCAN_OPTIONS slots, including padding. Treating
 * it as a NUL-terminated string would give it a different boundary.
 */
#define NUM_SCAN_OPTIONS 11

#define DEFAULT_ICONS_DIR L"icons"

#define GRAPHICS_FOR_OSX        1
#define GRAPHICS_FOR_LINUX      2
#define GRAPHICS_FOR_ELILO      4
#define GRAPHICS_FOR_GRUB       8
#define GRAPHICS_FOR_WINDOWS   16

#define LEGACY_TYPE_NONE 0
#define LEGACY_TYPE_MAC  1
#define LEGACY_TYPE_UEFI 2

#define DISCOVERY_TYPE_UNKNOWN  0
#define DISCOVERY_TYPE_AUTO     1
#define DISCOVERY_TYPE_MANUAL   2

#ifdef __MAKEWITH_GNUEFI

#define BBS_FLOPPY        0x01
#define BBS_HARDDISK      0x02
#define BBS_CDROM         0x03
#define BBS_PCMCIA        0x04
#define BBS_USB           0x05
#define BBS_EMBED_NETWORK 0x06
#define BBS_BEV_DEVICE    0x80
#define BBS_UNKNOWN       0xff
#endif

#define DEVICE_TYPE_HW         0x01
#define DEVICE_TYPE_ACPI       0x02  
#define DEVICE_TYPE_MESSAGING  0x03
#define DEVICE_TYPE_MEDIA      0x04  
#define DEVICE_TYPE_BIOS       0x05  
#define DEVICE_TYPE_END        0x75  

#define FS_TYPE_UNKNOWN        0
#define FS_TYPE_WHOLEDISK      1
#define FS_TYPE_FAT            2
#define FS_TYPE_EXFAT          3
#define FS_TYPE_NTFS           4
#define FS_TYPE_EXT2           5
#define FS_TYPE_EXT3           6
#define FS_TYPE_EXT4           7
#define FS_TYPE_HFSPLUS        8
#define FS_TYPE_REISERFS       9
#define FS_TYPE_BTRFS          10
#define FS_TYPE_XFS            11
#define FS_TYPE_JFS            12
#define FS_TYPE_ISO9660        13
#define NUM_FS_TYPES           14

#define BANNER_NOSCALE         0
#define BANNER_FILLSCREEN      1

#define DEFAULT_SMALL_ICON_SIZE 48
#define DEFAULT_BIG_ICON_SIZE   104
#define DEFAULT_MOUSE_SIZE      16

#define ICON_SIZE_BADGE 0
#define ICON_SIZE_SMALL 1
#define ICON_SIZE_BIG   2
#define ICON_SIZE_MOUSE 3

#define HIDPI_MIN 1921

#ifndef EFI_OS_INDICATIONS_BOOT_TO_FW_UI
#define EFI_OS_INDICATIONS_BOOT_TO_FW_UI 0x0000000000000001ULL
#endif

#if defined (EFIX64)
#define FWUPDATE_NAMES          L"fwupx64.efi"
#define MEMTEST_NAMES           L"memtest86.efi,memtest86_x64.efi,memtest86x64.efi,memtest86+x64.efi"
#define MOK_NAMES               L"MokManager.efi,HashTool.efi,HashTool-signed.efi,KeyTool.efi,KeyTool-signed.efi,mmx64.efi"
#define SHELL_NAMES             L"shell.efi,shellx64.efi"
#elif defined(EFI32)
#define FWUPDATE_NAMES          L"fwupia32.efi"
#define MEMTEST_NAMES           L"memtest86.efi,memtest86_ia32.efi,memtest86ia32.efi,memtest86+ia32.efi"
#define MOK_NAMES               L"MokManager.efi,HashTool.efi,HashTool-signed.efi,KeyTool.efi,KeyTool-signed.efi,mmia32.efi"
#define SHELL_NAMES             L"shell.efi,shellia32.efi"
#elif defined(EFIAARCH64)
#define FWUPDATE_NAMES          L"fwupaa64.efi"
#define MEMTEST_NAMES           L"memtest86.efi,memtest86_aa64.efi,memtest86aa64.efi"
#define MOK_NAMES               L"MokManager.efi,HashTool.efi,HashTool-signed.efi,KeyTool.efi,KeyTool-signed.efi,mmaa64.efi"
#define SHELL_NAMES             L"shell.efi,shellaa64.efi"
#else
#define FWUPDATE_NAMES          L"fwup.efi"
#define MEMTEST_NAMES           L"memtest86.efi"
#define MOK_NAMES               L"MokManager.efi,HashTool.efi,HashTool-signed.efi,KeyTool.efi,KeyTool-signed.efi"
#define SHELL_NAMES             L"shell.efi"
#endif

#define TOOL_LOCATIONS       L"EFI\\tools"

#define WINDOWS_RECOVERY_FILES  L"EFI\\Microsoft\\Boot\\LrsBootmgr.efi,Recovery:\\EFI\\BOOT\\bootx64.efi,Recovery:\\EFI\\BOOT\\bootia32.efi,\\EFI\\OEM\\Boot\\bootmgfw.efi"

#define MACOS_RECOVERY_FILES    L"com.apple.recovery.boot\\boot.efi"

#define LOADER_MATCH_PATTERNS   L"*.efi,*.EFI"

#if defined(EFIAARCH64)
#define LINUX_PREFIXES          L"vmlinuz,Image,kernel"
#else
#define LINUX_PREFIXES          L"vmlinuz,bzImage,kernel"
#endif

#define HIDEUI_FLAG_NONE       (0x0000)
#define HIDEUI_FLAG_BANNER     (0x0001)
#define HIDEUI_FLAG_LABEL      (0x0002)
#define HIDEUI_FLAG_SINGLEUSER (0x0004)
#define HIDEUI_FLAG_HWTEST     (0x0008)
#define HIDEUI_FLAG_ARROWS     (0x0010)
#define HIDEUI_FLAG_HINTS      (0x0020)
#define HIDEUI_FLAG_EDITOR     (0x0040)
#define HIDEUI_FLAG_SAFEMODE   (0x0080)
#define HIDEUI_FLAG_BADGES     (0x0100)
#define HIDEUI_FLAG_ALL       ((0xffff))

#define SUBSCREEN_HINT1            L"Use arrow keys to move cursor; Enter to boot;"
#define SUBSCREEN_HINT2            L"Insert or F2 to edit options; Esc to return to main menu"
#define SUBSCREEN_HINT2_NO_EDITOR  L"Esc to return to main menu"

#define NULL_GUID_VALUE { 0x00000000, 0x0000, 0x0000, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} };
#define BOOT_GUID_VALUE { 0x36D08FA7, 0xCF0B, 0x42F5, {0x8F, 0x14, 0x68, 0xDF, 0x73, 0xED, 0x37, 0x40} };
#define ESP_GUID_VALUE { 0xc12a7328, 0xf81f, 0x11d2, { 0xba, 0x4b, 0x00, 0xa0, 0xc9, 0x3e, 0xc9, 0x3b } };
#define SYSTEMD_GUID_VALUE { 0x4a67b082, 0x0a4c, 0x41cf, { 0xb6, 0xc7, 0x44, 0x0b, 0x29, 0xbb, 0x8c, 0x4f }};

#define KERNEL_VERSION L"%v"
#define MAX_RES_CODE 2147483647  

typedef struct _uint32_list {
    UINT32               Value;
    struct _uint32_list  *Next;
} UINT32_LIST;
/*
 * The four partition descriptors begin at byte 446 of an MBR. StartLBA and Size
 * are sector counts. In an extended-partition record the same StartLBA field has
 * two origins, depending on whether it describes a logical partition or the next
 * link in the chain.
 */
typedef struct {
   UINT8 Flags;
   UINT8 StartCHS1;
   UINT8 StartCHS2;
   UINT8 StartCHS3;
   UINT8 Type;
   UINT8 EndCHS1;
   UINT8 EndCHS2;
   UINT8 EndCHS3;
   UINT32 StartLBA;
   UINT32 Size;
} MBR_PARTITION_INFO;
/*
 * A volume can have a Block I/O handle even when the firmware has no
 * filesystem protocol for it. RootDir is needed for file discovery.
 * WholeDiskBlockIO and BlockIOOffset identify the backing disk used for
 * partition-level work.
 */
typedef struct {
   EFI_DEVICE_PATH     *DevicePath;
   EFI_HANDLE          DeviceHandle;
   EFI_FILE_PROTOCOL   *RootDir;
   CHAR16              *PartName;  
   CHAR16              *FsName;    
   CHAR16              *VolName;   
   /*
    * VolUuid identifies a filesystem during duplicate-volume suppression.
    * PartGuid identifies this GPT partition and forms the PARTUUID passed to
    * Linux. PartTypeGuid identifies the partition's role. Two root partitions can
    * share a type GUID and still require different kernel arguments.
    */
   EFI_GUID            VolUuid;
   EFI_GUID            PartGuid;
   EFI_GUID            PartTypeGuid;
   BOOLEAN             IsMarkedReadOnly;
   EG_IMAGE            *VolIconImage;
   EG_IMAGE            *VolBadgeImage;
   UINTN               DiskKind;
   BOOLEAN             HasBootCode;
   CHAR16              *OSIconName;
   CHAR16              *OSName;
   BOOLEAN             IsMbrPartition;
   UINTN               MbrPartitionIndex;
   /*
    * BlockIO can address either a firmware partition or the containing disk. A
    * synthetic logical partition borrows the whole-disk interface and adds
    * BlockIOOffset in sectors. A native partition handle already has its own
    * addressing origin.
    */
   EFI_BLOCK_IO        *BlockIO;
   UINT64              BlockIOOffset;
   EFI_BLOCK_IO        *WholeDiskBlockIO;
   EFI_DEVICE_PATH     *WholeDiskDevicePath;
   MBR_PARTITION_INFO  *MbrPartitionTable;
   BOOLEAN             IsReadable;
   UINT32              FSType;
} REFIT_VOLUME;
/*
 * Menu painting and input use this common prefix. Row zero is the scrolling
 * loader row and row one contains tools. A SubScreen is entered on the details
 * action. Choosing an entry returns the existing pointer from the screen's
 * Entries array.
 */
typedef struct _refit_menu_entry {
   CHAR16      *Title;
   UINTN       Tag;
   UINTN       Row;
   CHAR16      ShortcutDigit;
   CHAR16      ShortcutLetter;
   EG_IMAGE    *Image;
   EG_IMAGE    *BadgeImage;
   struct _refit_menu_screen *SubScreen;
} REFIT_MENU_ENTRY;
/*
 * Entries and InfoLines are counted pointer arrays. TimeoutSeconds uses zero for
 * interactive selection, a positive countdown in seconds, and the UINTN
 * representation of -1 for the pending-key shortcut. RunMainMenu disables the
 * initial timeout when returning from a submenu.
 */
typedef struct _refit_menu_screen {
   CHAR16      *Title;  
   EG_IMAGE    *TitleImage;
   UINTN       InfoLineCount;
   CHAR16      **InfoLines;
   UINTN       EntryCount;      
   REFIT_MENU_ENTRY **Entries;
   UINTN       TimeoutSeconds;
   CHAR16      *TimeoutText;
   CHAR16      *Hint1;
   CHAR16      *Hint2;
} REFIT_MENU_SCREEN;
/*
 * Place the generic menu entry first in a loader or legacy record. Menu code
 * stores a REFIT_MENU_ENTRY pointer. The tag tells dispatch which enclosing
 * record it may recover at that address.
 */
typedef struct {
   REFIT_MENU_ENTRY me;
   CHAR16           *Title;  
   CHAR16           *LoaderPath;
   REFIT_VOLUME     *Volume;
   BOOLEAN          UseGraphicsMode;
   BOOLEAN          Enabled;
   CHAR16           *LoadOptions;
   CHAR16           *InitrdPath;  
   CHAR8            OSType;
   UINTN            DiscoveryType;
   /*
    * Firmware entries carry EfiBootNum for BootNext. A discovered firmware entry
    * also retains a copied device path for display and diagnostics. File-backed
    * entries carry Volume and LoaderPath for StartEFIImage instead.
    */
   EFI_DEVICE_PATH  *EfiLoaderPath;  
   UINT16           EfiBootNum;  
} LOADER_ENTRY;

typedef struct {
   REFIT_MENU_ENTRY  me;
   REFIT_VOLUME      *Volume;
   BDS_COMMON_OPTION *BdsOption;
   CHAR16            *LoadOptions;
   BOOLEAN           Enabled;
} LEGACY_ENTRY;
/*
 * ReadConfig updates this process-wide state in directive order. Some fields are
 * derived later: ScanVolumes supplies DiscoveredRoot, scanning can extend
 * ExtraToolLocations, and LinuxMatchPatterns is constructed from LinuxPrefixes.
 */
typedef struct {
   BOOLEAN          TextOnly;
   BOOLEAN          ScanAllLinux;
   BOOLEAN          DeepLegacyScan;
   BOOLEAN          EnableAndLockVMX;
   BOOLEAN          FoldLinuxKernels;
   BOOLEAN          EnableMouse;
   BOOLEAN          EnableTouch;
   BOOLEAN          HiddenTags;
   BOOLEAN          UseNvram;
   BOOLEAN          ShutdownAfterTimeout;
   BOOLEAN          Install;
   BOOLEAN          WriteSystemdVars;
   BOOLEAN          FollowSymlinks;
   BOOLEAN          GzippedLoaders;
   UINTN            RequestedScreenWidth;
   UINTN            RequestedScreenHeight;
   UINTN            BannerBottomEdge;
   UINTN            RequestedTextMode;
   UINTN            Timeout;
   UINTN            HideUIFlags;
   UINTN            MaxTags;      
   UINTN            GraphicsFor;
   UINTN            LegacyType;
   UINTN            ScanDelay;
   UINTN            ScreensaverTime;
   UINTN            MouseSpeed;
   UINTN            IconSizes[4];
   UINTN            BannerScale;
   UINTN            LogLevel;
   REFIT_VOLUME     *DiscoveredRoot;
   EFI_DEVICE_PATH  *SelfDevicePath;
   CHAR16           *BannerFileName;
   EG_IMAGE         *ScreenBackground;
   CHAR16           *ConfigFilename;
   CHAR16           *SelectionSmallFileName;
   CHAR16           *SelectionBigFileName;
   CHAR16           *DefaultSelection;
   CHAR16           *AlsoScan;
   CHAR16           *ToolLocations;
   CHAR16           *ExtraToolLocations;
   CHAR16           *DontScanVolumes;
   CHAR16           *DontScanDirs;
   CHAR16           *DontScanFiles;
   CHAR16           *DontScanTools;
   CHAR16           *DontScanFirmware;
   CHAR16           *WindowsRecoveryFiles;
   CHAR16           *MacOSRecoveryFiles;
   CHAR16           *DriverDirs;
   CHAR16           *IconsDir;
   CHAR16           *LinuxPrefixes;       
   CHAR16           *LinuxMatchPatterns;  
   CHAR16           *ExtraKernelVersionStrings;
   CHAR16           *SpoofOSXVersion;
   UINT32_LIST      *CsrValues;
   /*
    * ShowTools holds action tags used while building row one. ScanFor holds
    * single-byte letters in the user's discovery order. Their capacities bound
    * loops in different consumers and their element values have different
    * meanings.
    */
   UINTN            ShowTools[NUM_TOOLS];
   CHAR8            ScanFor[NUM_SCAN_OPTIONS];
   /* Optional PNG/JPEG artwork loaded entirely from boot.conf. The trailing
    * fields default to NULL in the existing positional GlobalConfig init. */
   CHAR16           *ThemeBackgroundFileName;
   CHAR16           *ThemeLogoFileName;
   CHAR16           *ThemeMarkFileName;
   CHAR16           *ThemeUnderlineFileName;
} REFIT_CONFIG;

/*
 * Image/volume handles are borrowed from firmware and may become invalid after
 * teardown. SelfDir and SelfRootDir are open EFI file handles; Volumes is a
 * process-owned scan inventory and cannot serve as persistent device identity.
 */
extern EFI_HANDLE         SelfImageHandle;
extern EFI_LOADED_IMAGE   *SelfLoadedImage;
extern EFI_FILE_PROTOCOL  *SelfRootDir;
extern EFI_FILE_PROTOCOL  *SelfDir;
extern CHAR16             *SelfDirPath;

extern REFIT_VOLUME     *SelfVolume;
extern REFIT_VOLUME     **Volumes;
extern UINTN            VolumesCount;

extern REFIT_CONFIG     GlobalConfig;
extern CHAR16           *gHiddenTools;

extern EFI_GUID gEfiLegacyBootProtocolGuid;
extern EFI_GUID gEfiGlobalVariableGuid;

extern BOOLEAN HaveResized;

extern EFI_GUID GlobalGuid;
extern EFI_GUID BootGuid;

extern REFIT_MENU_SCREEN MainMenu;
extern REFIT_MENU_ENTRY MenuEntryReturn;

/*
 * Boot UI actions operate on the current inventory. RescanAll rebuilds entries
 * and invalidates borrowed menu pointers; each returned loader record follows
 * the menu's ownership rules.
 */
VOID AboutBOOT(VOID);
EG_IMAGE * GetDiskBadge(IN UINTN DiskType);
LOADER_ENTRY * MakeGenericLoaderEntry(VOID);
VOID StoreLoaderName(IN CHAR16 *Name);
VOID RescanAll(BOOLEAN DisplayMessage, BOOLEAN Reconnect);
/*
 * gunzip writes to out_buf or a flush callback. On success, decoded_size
 * receives the total number of output bytes. On failure it receives zero.
 * Callers that do not need the byte count may pass NULL.
 */
int gunzip(unsigned char *buf, long len,
           long (*fill)(void*, unsigned long),
           long (*flush)(void*, unsigned long),
           unsigned char *out_buf, long out_len,
           UINTN *decoded_size);

#endif
