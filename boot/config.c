/*
 * BOOT / Configuration
 *
 * Volume-resident configuration is untrusted input. It determines scanning
 * and presentation policy; its tokens are neither validated filesystem paths
 * nor authorisation to launch a payload.
 *
 * BOOT-owned variable storage remains distinct from firmware's global
 * BootOrder.
 */

#include "global.h"
#include "lib.h"
#include "icns.h"
#include "log.h"
#include "menu.h"
#include "config.h"
#include "screen.h"
#include "apple.h"
#include "mystrings.h"
#include "scan.h"
#include "../include/boot_call_wrapper.h"
#include "../mok/mok.h"

#define LINUX_OPTIONS_FILENAMES  L"boot_linux.conf,boot-linux.conf"
#define MAXCONFIGFILESIZE        (128*1024)

#define ENCODING_ISO8859_1  (0)
#define ENCODING_UTF8       (1)
#define ENCODING_UTF16_LE   (2)

#define GetTime ST->RuntimeServices->GetTime
#define LAST_MINUTE 1439  

/**
 * Populate the byte and CHAR16 cursors over one pool buffer. File->BufferSize is
 * the actual count returned by Read, while *size is set from file metadata before
 * the read. The UTF-16 end cursor drops an odd trailing byte.
 *
 * A BOM is considered only when at least four bytes were read. Without one,
 * zeroes in bytes 1 and 3 select UTF-16LE. All other input starts in the
 * single-byte path. On failure the output size is zeroed and the temporary
 * firmware file handle is closed. A successful call transfers File->Buffer
 * to the caller; the diagnostic message is not part of that allocation.
 *
 * LIMITATION: FileInfo->FileSize is UINT64 but is narrowed to UINTN before the
 * pool allocation without an explicit representability check. The file's
 * actual read byte count replaces BufferSize, while the separate output size
 * is set before reading. A successful EFI Read does not by itself prove the
 * entire advertised file was consumed. UTF-8 detection does not imply UTF-8
 * decoding: ReadLine widens individual bytes for this encoding label.
 *
 * Two lengths represent different things after Read: the caller-visible
 * output size is derived from the file metadata, while the buffer's current
 * byte count follows EFI Read's in/out parameter. Downstream parsing must
 * not assume those lengths are equal on a short successful read. A BOM-based
 * classification is not evidence that the remaining file is well-formed
 * Unicode. These limitations are source-observed and require regression
 * fixtures before an encoding or input-size guarantee can be made.
 */
EFI_STATUS ReadFile(IN EFI_FILE_HANDLE BaseDir, IN CHAR16 *FileName, IN OUT BOOT_FILE *File, OUT UINTN *size)
{
    EFI_STATUS      Status;
    EFI_FILE_HANDLE FileHandle;
    EFI_FILE_INFO   *FileInfo;
    UINT64          ReadSize;
    CHAR16          *Message;

    if (!BaseDir || !FileName || !File || !size)
        return EFI_INVALID_PARAMETER;
    *size = 0;
    File->Buffer = NULL;
    File->BufferSize = 0;

    Status = boot_call5_wrapper(BaseDir->Open, BaseDir, &FileHandle, FileName, EFI_FILE_MODE_READ, 0);
    Message = PoolPrint(L"while loading the file '%s'", FileName);
    if (CheckError(Status, Message)) {
        MyFreePool(Message);
        return Status;
    }

    FileInfo = LibFileInfo(FileHandle);
    if (FileInfo == NULL) {
        MyFreePool(Message);
        boot_call1_wrapper(FileHandle->Close, FileHandle);
        return EFI_LOAD_ERROR;
    }
    ReadSize = FileInfo->FileSize;
    MyFreePool(FileInfo);

    File->BufferSize = (UINTN)ReadSize;
    File->Buffer = AllocatePool(File->BufferSize);
    if (File->Buffer == NULL) {
        File->BufferSize = 0;
        MyFreePool(Message);
        boot_call1_wrapper(FileHandle->Close, FileHandle);
        return EFI_OUT_OF_RESOURCES;
    }
    *size = File->BufferSize;  
    Status = boot_call3_wrapper(FileHandle->Read, FileHandle, &File->BufferSize, File->Buffer);
    if (CheckError(Status, Message)) {
        MyFreePool(File->Buffer);
        MyFreePool(Message);
        File->Buffer = NULL;
        boot_call1_wrapper(FileHandle->Close, FileHandle);
        return Status;
    }
    Status = boot_call1_wrapper(FileHandle->Close, FileHandle);

    File->Current8Ptr  = (CHAR8 *)File->Buffer;
    File->End8Ptr      = File->Current8Ptr + File->BufferSize;
    File->Current16Ptr = (CHAR16 *)File->Buffer;
    File->End16Ptr     = File->Current16Ptr + (File->BufferSize >> 1);

    File->Encoding = ENCODING_ISO8859_1;    
    if (File->BufferSize >= 4) {
        if (File->Buffer[0] == 0xFF && File->Buffer[1] == 0xFE) {

            File->Encoding = ENCODING_UTF16_LE;    
            File->Current16Ptr++;
        } else if (File->Buffer[0] == 0xEF && File->Buffer[1] == 0xBB && File->Buffer[2] == 0xBF) {

            File->Encoding = ENCODING_UTF8;        
            File->Current8Ptr += 3;
        } else if (File->Buffer[1] == 0 && File->Buffer[3] == 0) {
            File->Encoding = ENCODING_UTF16_LE;    
        }

    }

    MyFreePool(Message);
    return EFI_SUCCESS;
}

/**
 * Consume through the next CR or LF, then skip the entire run of line
 * terminators. End pointers delimit the file without requiring a NUL on disk. A
 * successful call returns a separately allocated, terminated CHAR16 line. A NULL
 * result covers end of input and allocation failure.
 *
 * The UTF-8-labelled branch widens one input byte per output code unit just as
 * the single-byte branch does. Only the UTF-16LE branch copies existing 16-bit
 * units. This distinction matters for non-ASCII loader titles.
 */
static CHAR16 *ReadLine(BOOT_FILE *File)
{
    CHAR16  *Line, *q;
    UINTN   LineLength;

    if (File->Buffer == NULL)
        return NULL;

    if (File->Encoding == ENCODING_ISO8859_1 || File->Encoding == ENCODING_UTF8) {

        CHAR8 *p, *LineStart, *LineEnd;

        p = File->Current8Ptr;
        if (p >= File->End8Ptr)
            return NULL;

        LineStart = p;
        for (; p < File->End8Ptr; p++)
            if (*p == 13 || *p == 10)
                break;
        LineEnd = p;
        for (; p < File->End8Ptr; p++)
            if (*p != 13 && *p != 10)
                break;
        File->Current8Ptr = p;

        LineLength = (UINTN)(LineEnd - LineStart) + 1;
        Line = AllocatePool(LineLength * sizeof(CHAR16));
        if (Line == NULL)
            return NULL;

        q = Line;
        if (File->Encoding == ENCODING_ISO8859_1) {
            for (p = LineStart; p < LineEnd; )
                *q++ = *p++;
        } else if (File->Encoding == ENCODING_UTF8) {

            for (p = LineStart; p < LineEnd; )
                *q++ = *p++;
        }
        *q = 0;

    } else if (File->Encoding == ENCODING_UTF16_LE) {

        CHAR16 *p, *LineStart, *LineEnd;

        p = File->Current16Ptr;
        if (p >= File->End16Ptr)
            return NULL;

        LineStart = p;
        for (; p < File->End16Ptr; p++)
            if (*p == 13 || *p == 10)
                break;
        LineEnd = p;
        for (; p < File->End16Ptr; p++)
            if (*p != 13 && *p != 10)
                break;
        File->Current16Ptr = p;

        LineLength = (UINTN)(LineEnd - LineStart) + 1;
        Line = AllocatePool(LineLength * sizeof(CHAR16));
        if (Line == NULL)
            return NULL;

        for (p = LineStart, q = Line; p < LineEnd; )
            *q++ = *p++;
        *q = 0;

    } else
        return NULL;    

    return Line;
}

/**
 * Advance the tokenizer's quote state at one code unit. Space, tab, equals, comma
 * and hash terminate an unquoted field. A doubled quote collapses to one literal
 * quote in the mutable line, whereas a single quote changes the state and ends
 * this token fragment.
 */
static BOOLEAN KeepReading(IN OUT CHAR16 *p, IN OUT BOOLEAN *IsQuoted) {
    BOOLEAN MoreToRead = FALSE;
    CHAR16  *Temp = NULL;

    if ((p == NULL) || (IsQuoted == NULL))
        return FALSE;

    if (*p == L'\0')
        return FALSE;

    if ((*p != ' ' && *p != '\t' && *p != '=' && *p != '#' && *p != ',') || *IsQuoted) {
        MoreToRead = TRUE;
    }
    if (*p == L'"') {
        if (p[1] == L'"') {
            Temp = StrDuplicate(&p[1]);
            if (Temp != NULL) {
                StrCpy(p, Temp);
                MyFreePool(Temp);
            }
            MoreToRead = TRUE;
        } else {
            *IsQuoted = !(*IsQuoted);
            MoreToRead = FALSE;
        }  
    }  

    return MoreToRead;
}  

/**
 * Skip lines until at least one field is found. Outside quotes, equals and comma
 * are separators, a hash ends the line, and forward slashes become EFI path
 * separators. Quoted fields preserve spaces and forward slashes, which matters
 * for Linux root arguments.
 *
 * Each token is duplicated before the temporary line is freed. The returned count
 * bounds the array. FreeTokenLine releases the strings and array together. Zero
 * also ends parsing when the line reader cannot allocate.
 */
UINTN ReadTokenLine(IN BOOT_FILE *File, OUT CHAR16 ***TokenList)
{
    BOOLEAN         LineFinished, IsQuoted = FALSE;
    CHAR16          *Line, *Token, *p;
    UINTN           TokenCount = 0;

    *TokenList = NULL;

    while (TokenCount == 0) {
        Line = ReadLine(File);
        if (Line == NULL)
            return(0);

        p = Line;
        LineFinished = FALSE;
        while (!LineFinished) {

            while ((*p == ' ' || *p == '\t' || *p == '=' || *p == ',') && !IsQuoted)
                p++;
            if (*p == 0 || *p == '#')
                break;

            if (*p == '"') {
               IsQuoted = !IsQuoted;
               p++;
            }  
            Token = p;

            while (KeepReading(p, &IsQuoted)) {
               if ((*p == L'/') && !IsQuoted)  
                  *p = L'\\';
               p++;
            }  
            if (*p == L'\0' || *p == L'#')
                LineFinished = TRUE;
            *p++ = 0;

            AddListElement((VOID ***)TokenList, &TokenCount, (VOID *)StrDuplicate(Token));
        }

        MyFreePool(Line);
    }
    return (TokenCount);
}  

/*
 * Releases parser tokens and resets their caller-visible ownership state.
 * No downstream configuration field may retain borrowed token pointers.
 */
VOID FreeTokenLine(IN OUT CHAR16 ***TokenList, IN OUT UINTN *TokenCount)
{

    FreeList((VOID ***)TokenList, TokenCount);
}

/*
 * Read exactly one numeric argument into UINTN. The text -1 becomes an
 * all-ones sentinel; other values pass through Atoi without a
 * field-specific range check. If the token count is not two, the prior
 * destination value is unchanged.
 */
static VOID HandleInt(IN CHAR16 **TokenList, IN UINTN TokenCount, OUT UINTN *Value)
{
    if (TokenCount == 2) {
       if (StrCmp(TokenList[1], L"-1") == 0)
          *Value = -1;
       else
          *Value = Atoi(TokenList[1]);
    }
}

/*
 * Replace an owned configuration string with a duplicate of the second
 * token. The previous allocation is freed before StrDuplicate succeeds; a
 * pool-allocation failure can therefore leave Target null. The parser token
 * can be released after the copy.
 */
static VOID HandleString(IN CHAR16 **TokenList, IN UINTN TokenCount, OUT CHAR16 **Target) {
    if ((TokenCount == 2) && Target) {
        MyFreePool(*Target);
        *Target = StrDuplicate(TokenList[1]);
    }  
}  

/**
 * A leading "+" with at least one following token appends to the current list.
 * All other forms first discard the old value, so a directive with no arguments
 * can clear it. Each retained field is path-normalised before being joined with
 * commas.
 */
static VOID HandleStrings(IN CHAR16 **TokenList, IN UINTN TokenCount, OUT CHAR16 **Target) {
    UINTN i;
    BOOLEAN AddMode = FALSE;

    if ((TokenCount > 2) && (StrCmp(TokenList[1], L"+") == 0)) {
        AddMode = TRUE;
    }

    if ((*Target != NULL) && !AddMode) {
        MyFreePool(*Target);
        *Target = NULL;
    }  
    for (i = 1; i < TokenCount; i++) {
        if ((i != 1) || !AddMode) {
            CleanUpPathNameSlashes(TokenList[i]);
            MergeStrings(Target, TokenList[i], L',');
        }  
    }  
}  

/**
 * Replace the linked list unless a leading "+" requests an append. Accept only
 * tokens that pass IsValidHex and whose parsed value is at most MaxValue.
 * Allocation occurs per accepted value, so failure can leave a partially built
 * list.
 */
static VOID HandleHexes(IN CHAR16 **TokenList,
                        IN UINTN TokenCount,
                        IN UINTN MaxValue,
                        OUT UINT32_LIST **Target) {
    UINTN       InputIndex = 1, i;
    UINT32      Value;
    UINT32_LIST *EndOfList = NULL;
    UINT32_LIST *NewEntry;

    if ((TokenCount > 2) && (StrCmp(TokenList[1], L"+") == 0)) {
        InputIndex = 2;
        EndOfList = *Target;
        while (EndOfList && (EndOfList->Next != NULL)) {
            EndOfList = EndOfList->Next;
        }
    } else {
        EraseUint32List(Target);
    }

    for (i = InputIndex; i < TokenCount; i++) {
        if (IsValidHex(TokenList[i])) {
            Value = (UINT32) StrToHex(TokenList[i], 0, 8);
            if (Value <= MaxValue) {
                NewEntry = AllocatePool(sizeof(UINT32_LIST));
                if (NewEntry) {
                    NewEntry->Value = Value;
                    NewEntry->Next = NULL;
                    if (EndOfList == NULL) {
                        EndOfList = NewEntry;
                        *Target = NewEntry;
                    } else {
                        EndOfList->Next = NewEntry;
                        EndOfList = NewEntry;
                    }  
                }  
            }  
        }  
    }  
}  

/**
 * Accumulate decimal digits until a colon moves the current number into Hour. The
 * return value is minutes since midnight. Fields are not validated individually
 * here, so the caller's upper-bound check operates on the combined value.
 */
static UINTN HandleTime(IN CHAR16 *TimeString) {
    UINTN Hour = 0, Minute = 0, TimeLength, i = 0;

    TimeLength = StrLen(TimeString);
    while (i < TimeLength) {
        if (TimeString[i] == L':') {
            Hour = Minute;
            Minute = 0;
        }  
        if ((TimeString[i] >= L'0') && (TimeString[i] <= '9')) {
            Minute *= 10;
            Minute += (TimeString[i] - L'0');
        }  
        i++;
    }  
    return (Hour * 60 + Minute);
}  

/*
 * Absent arguments default to true. The literal values 0, false and off
 * select false using BOOT's ASCII-oriented comparison; this is a directive
 * convention, not a general Boolean parser with strict token validation.
 */
static BOOLEAN HandleBoolean(IN CHAR16 **TokenList, IN UINTN TokenCount) {
    BOOLEAN TruthValue = TRUE;

    if ((TokenCount >= 2) && ((StrCmp(TokenList[1], L"0") == 0) ||
                              MyStriCmp(TokenList[1], L"false") ||
                              MyStriCmp(TokenList[1], L"off"))) {
        TruthValue = FALSE;
    }  

    return TruthValue;
}  

/**
 * Convert the firmware clock and both endpoints to minutes. Both branches use the
 * same inclusive comparison. Equal endpoints select one minute. An interval such
 * as 22:00 to 06:00 never satisfies both comparisons. If the clock read fails the
 * prior default remains in effect.
 */
static VOID SetDefaultByTime(IN CHAR16 **TokenList, OUT CHAR16 **Default) {
    EFI_STATUS            Status;
    EFI_TIME              CurrentTime;
    UINTN                 StartTime, EndTime, Now;
    BOOLEAN               SetIt = FALSE;

    StartTime = HandleTime(TokenList[2]);
    EndTime = HandleTime(TokenList[3]);

    if ((StartTime <= LAST_MINUTE) && (EndTime <= LAST_MINUTE)) {
        Status = boot_call2_wrapper(GetTime, &CurrentTime, NULL);
        if (Status != EFI_SUCCESS)
            return;
        Now = CurrentTime.Hour * 60 + CurrentTime.Minute;

        if (Now > LAST_MINUTE) {  
            Print(L"Warning: Impossible system time: %d:%d\n", CurrentTime.Hour, CurrentTime.Minute);
            return;
        }  

        if (StartTime < EndTime) {  
            if ((Now >= StartTime) && (Now <= EndTime))
                SetIt = TRUE;
        } else {  
            if ((Now >= StartTime) && (Now <= EndTime))
                SetIt = TRUE;
        }  

        if (SetIt) {
            MyFreePool(*Default);
            *Default = StrDuplicate(TokenList[1]);
        }  
    }  
}  

/*
 * Publish the prepared entry pointer directly into the global menu.
 * No deep copy or allocation-success result is provided by AddMenuEntry:
 * the caller retains the lifetime constraints of the menu inventory.
 */
static LOADER_ENTRY * AddPreparedLoaderEntry(LOADER_ENTRY *Entry) {
    AddMenuEntry(&MainMenu, (BOOT_MENU_ENTRY *)Entry);

    return(Entry);
}  

/**
 * Turn each Linux filename prefix into a wildcard by appending "*", then replace
 * the comma-separated pattern list. Prefixes remain available separately for
 * classifying candidates after directory enumeration.
 */
static VOID SetLinuxMatchPatterns(CHAR16 *Prefixes) {
    CHAR16 *Pattern, *PatternSet = NULL;
    UINTN i = 0;

    while ((Pattern = FindCommaDelimited(Prefixes, i++)) != NULL) {
        MergeStrings(&Pattern, L"*", 0);
        MergeStrings(&PatternSet, Pattern, L',');
        MyFreePool(Pattern);
    }
    if (GlobalConfig.LinuxMatchPatterns)
        MyFreePool(GlobalConfig.LinuxMatchPatterns);
    GlobalConfig.LinuxMatchPatterns = PatternSet;
}  

/*
 * Reading the primary file rebuilds the scan defaults and the '+' menu
 * preference. An included file applies directives to the state accumulated so
 * far. Resetting those lists for every include would discard the effect of
 * earlier directives.
 */
/*
 * Read only the diagnostic flag ahead of the first ScanVolumes call. Full
 * ReadConfig cannot run yet: other directives can refer to discovered volumes.
 * Both passes use the exact existing token and Boolean parser, so the setting
 * has identical semantics regardless of startup phase. An absent directive
 * preserves the runtime default; the primary boot.conf owns the early switch.
 */
VOID ReadStartupDiagnosticsSetting(CHAR16 *FileName)
{
    BOOT_FILE File;
    CHAR16 **Tokens;
    UINTN Count, Size;
    EFI_STATUS Status;

    if (!SelfDir || !FileName || !FileExists(SelfDir, FileName))
        return;

    Status = ReadFile(SelfDir, FileName, &File, &Size);
    if (EFI_ERROR(Status))
        return;

    while ((Count = ReadTokenLine(&File, &Tokens)) > 0) {
        if (MyStriCmp(Tokens[0], L"startup_diagnostics"))
            GlobalConfig.StartupDiagnostics = HandleBoolean(Tokens, Count);
        FreeTokenLine(&Tokens, &Count);
    }
    MyFreePool(File.Buffer);
}

VOID ReadConfig(CHAR16 *FileName)
{
    EFI_STATUS      Status;
    BOOT_FILE      File;
    CHAR16          **TokenList;
    CHAR16          *FlagName;
    CHAR16          *TempStr = NULL;
    UINTN           TokenCount, i;

    if (MyStriCmp(FileName, GlobalConfig.ConfigFilename)) {
        MyFreePool(GlobalConfig.AlsoScan);
        GlobalConfig.AlsoScan = StrDuplicate(ALSO_SCAN_DIRS);
        MyFreePool(GlobalConfig.DontScanDirs);
        if (SelfVolume)
            TempStr = GuidAsString(&(SelfVolume->PartGuid));
        MergeStrings(&TempStr, SelfDirPath, L':');
        if (GlobalConfig.ToolLocations == NULL)
            GlobalConfig.ToolLocations = StrDuplicate(TOOL_LOCATIONS);
        MergeStrings(&TempStr, TOOL_LOCATIONS, L',');
        GlobalConfig.DontScanDirs = TempStr;
        MyFreePool(GlobalConfig.DontScanFiles);
        GlobalConfig.DontScanFiles = StrDuplicate(DONT_SCAN_FILES);
        MyFreePool(GlobalConfig.DontScanTools);
        GlobalConfig.DontScanTools = NULL;
        MyFreePool(GlobalConfig.DontScanFirmware);
        GlobalConfig.DontScanFirmware = NULL;
        MergeStrings(&(GlobalConfig.DontScanFiles), MOK_NAMES, L',');
        MergeStrings(&(GlobalConfig.DontScanFiles), FWUPDATE_NAMES, L',');
        MergeStrings(&(GlobalConfig.DontScanFiles), MEMTEST_NAMES, L',');
        MergeStrings(&(GlobalConfig.DontScanFiles), SHELL_NAMES, L',');
        MyFreePool(GlobalConfig.DontScanVolumes);
        GlobalConfig.DontScanVolumes = StrDuplicate(DONT_SCAN_VOLUMES);
        GlobalConfig.WindowsRecoveryFiles = StrDuplicate(WINDOWS_RECOVERY_FILES);
        GlobalConfig.MacOSRecoveryFiles = StrDuplicate(MACOS_RECOVERY_FILES);
        MyFreePool(GlobalConfig.DefaultSelection);
        GlobalConfig.DefaultSelection = StrDuplicate(L"+");
        GlobalConfig.LinuxPrefixes = StrDuplicate(LINUX_PREFIXES);
    }  

    if (!FileExists(SelfDir, FileName)) {
        Print(L"Configuration file '%s' missing!\n", FileName);
        if (!FileExists(SelfDir, L"icons")) {
            Print(L"Icons directory doesn't exist; setting textonly = TRUE!\n");
            GlobalConfig.TextOnly = TRUE;
        }
        return;
    }

    Status = ReadFile(SelfDir, FileName, &File, &i);
    if (EFI_ERROR(Status))
        return;

    for (;;) {
        TokenCount = ReadTokenLine(&File, &TokenList);
        if (TokenCount == 0)
            break;

        if (MyStriCmp(TokenList[0], L"timeout")) {
            HandleInt(TokenList, TokenCount, &(GlobalConfig.Timeout));

        } else if (MyStriCmp(TokenList[0], L"shutdown_after_timeout")) {
           GlobalConfig.ShutdownAfterTimeout = HandleBoolean(TokenList, TokenCount);

        } else if (MyStriCmp(TokenList[0], L"hideui")) {
            for (i = 1; i < TokenCount; i++) {
                FlagName = TokenList[i];
                if (MyStriCmp(FlagName, L"banner")) {
                    GlobalConfig.HideUIFlags |= HIDEUI_FLAG_BANNER;
                } else if (MyStriCmp(FlagName, L"label")) {
                    GlobalConfig.HideUIFlags |= HIDEUI_FLAG_LABEL;
                } else if (MyStriCmp(FlagName, L"singleuser")) {
                    GlobalConfig.HideUIFlags |= HIDEUI_FLAG_SINGLEUSER;
                } else if (MyStriCmp(FlagName, L"hwtest")) {
                    GlobalConfig.HideUIFlags |= HIDEUI_FLAG_HWTEST;
                } else if (MyStriCmp(FlagName, L"arrows")) {
                    GlobalConfig.HideUIFlags |= HIDEUI_FLAG_ARROWS;
                } else if (MyStriCmp(FlagName, L"hints")) {
                    GlobalConfig.HideUIFlags |= HIDEUI_FLAG_HINTS;
                } else if (MyStriCmp(FlagName, L"editor")) {
                    GlobalConfig.HideUIFlags |= HIDEUI_FLAG_EDITOR;
                } else if (MyStriCmp(FlagName, L"safemode")) {
                    GlobalConfig.HideUIFlags |= HIDEUI_FLAG_SAFEMODE;
                } else if (MyStriCmp(FlagName, L"badges")) {
                    GlobalConfig.HideUIFlags |= HIDEUI_FLAG_BADGES;
                } else if (MyStriCmp(FlagName, L"all")) {
                    GlobalConfig.HideUIFlags = HIDEUI_FLAG_ALL;
                } else {
                    Print(L" unknown hideui flag: '%s'\n", FlagName);
                }
            }

        } else if (MyStriCmp(TokenList[0], L"icons_dir")) {
            HandleString(TokenList, TokenCount, &(GlobalConfig.IconsDir));

        } else if (MyStriCmp(TokenList[0], L"scanfor")) {
            /*
             * TokenList[0] is the directive, not a scan mode. Scan modes
             * begin at TokenList[1]; consuming the keyword as 's' wastes a
             * slot and can silently drop the last configured scan mode.
             * Commas and whitespace are both separators in ReadTokenLine.
             */
            for (i = 0; i < NUM_SCAN_OPTIONS; i++) {
                if (i + 1 < TokenCount)
                    GlobalConfig.ScanFor[i] = TokenList[i + 1][0];
                else
                    GlobalConfig.ScanFor[i] = ' ';
            }

        } else if (MyStriCmp(TokenList[0], L"follow_symlinks")) {
            GlobalConfig.FollowSymlinks = HandleBoolean(TokenList, TokenCount);

        } else if (MyStriCmp(TokenList[0], L"use_nvram")) {
            GlobalConfig.UseNvram = HandleBoolean(TokenList, TokenCount);

        } else if (MyStriCmp(TokenList[0], L"uefi_deep_legacy_scan")) {
            GlobalConfig.DeepLegacyScan = HandleBoolean(TokenList, TokenCount);

        } else if (MyStriCmp(TokenList[0], L"scan_delay") && (TokenCount == 2)) {
            HandleInt(TokenList, TokenCount, &(GlobalConfig.ScanDelay));

        } else if (MyStriCmp(TokenList[0], L"log_level") && (TokenCount == 2)) {
            HandleInt(TokenList, TokenCount, &(GlobalConfig.LogLevel));

        } else if (MyStriCmp(TokenList[0], L"startup_diagnostics") &&
                   MyStriCmp(FileName, GlobalConfig.ConfigFilename)) {
            /* Primary boot.conf is authoritative even during early scanning. */
            GlobalConfig.StartupDiagnostics = HandleBoolean(TokenList, TokenCount);

        } else if (MyStriCmp(TokenList[0], L"also_scan_dirs")) {
             HandleStrings(TokenList, TokenCount, &(GlobalConfig.AlsoScan));

        } else if (MyStriCmp(TokenList[0], L"don't_scan_volumes") ||
                   MyStriCmp(TokenList[0], L"dont_scan_volumes")) {

            MyFreePool(GlobalConfig.DontScanVolumes);
            GlobalConfig.DontScanVolumes = NULL;
            for (i = 1; i < TokenCount; i++) {
                MergeStrings(&GlobalConfig.DontScanVolumes, TokenList[i], L',');
            }

        } else if (MyStriCmp(TokenList[0], L"don't_scan_dirs") ||
                   MyStriCmp(TokenList[0], L"dont_scan_dirs")) {
            HandleStrings(TokenList, TokenCount, &(GlobalConfig.DontScanDirs));

        } else if (MyStriCmp(TokenList[0], L"don't_scan_files") ||
                   MyStriCmp(TokenList[0], L"dont_scan_files")) {
            HandleStrings(TokenList, TokenCount, &(GlobalConfig.DontScanFiles));

        } else if (MyStriCmp(TokenList[0], L"don't_scan_firmware") ||
                   MyStriCmp(TokenList[0], L"dont_scan_firmware")) {
            HandleStrings(TokenList, TokenCount, &(GlobalConfig.DontScanFirmware));

        } else if (MyStriCmp(TokenList[0], L"also_scan_tool_dirs")) {
            HandleStrings(TokenList, TokenCount, &(GlobalConfig.ExtraToolLocations));

        } else if (MyStriCmp(TokenList[0], L"don't_scan_tools") ||
                   MyStriCmp(TokenList[0], L"dont_scan_tools")) {
            HandleStrings(TokenList, TokenCount, &(GlobalConfig.DontScanTools));

        } else if (MyStriCmp(TokenList[0], L"windows_recovery_files")) {
            HandleStrings(TokenList, TokenCount, &(GlobalConfig.WindowsRecoveryFiles));

        } else if (MyStriCmp(TokenList[0], L"scan_driver_dirs")) {
            HandleStrings(TokenList, TokenCount, &(GlobalConfig.DriverDirs));

        } else if (MyStriCmp(TokenList[0], L"showtools")) {
            SetMem(GlobalConfig.ShowTools, NUM_TOOLS * sizeof(UINTN), 0);
            GlobalConfig.HiddenTags = FALSE;
            for (i = 1; (i < TokenCount) && (i < NUM_TOOLS); i++) {
                FlagName = TokenList[i];
                if (MyStriCmp(FlagName, L"shell")) {
                    GlobalConfig.ShowTools[i - 1] = TAG_SHELL;
                } else if (MyStriCmp(FlagName, L"gptsync")) {
                    GlobalConfig.ShowTools[i - 1] = TAG_GPTSYNC;
                } else if (MyStriCmp(FlagName, L"gdisk")) {
                   GlobalConfig.ShowTools[i - 1] = TAG_GDISK;
                } else if (MyStriCmp(FlagName, L"about")) {
                   GlobalConfig.ShowTools[i - 1] = TAG_ABOUT;
                } else if (MyStriCmp(FlagName, L"exit")) {
                   GlobalConfig.ShowTools[i - 1] = TAG_EXIT;
                } else if (MyStriCmp(FlagName, L"reboot")) {
                   GlobalConfig.ShowTools[i - 1] = TAG_REBOOT;
                } else if (MyStriCmp(FlagName, L"shutdown")) {
                   GlobalConfig.ShowTools[i - 1] = TAG_SHUTDOWN;
                } else if (MyStriCmp(FlagName, L"bootorder")) {
                   GlobalConfig.ShowTools[i - 1] = TAG_BOOTORDER;
                } else if (MyStriCmp(FlagName, L"apple_recovery")) {
                   GlobalConfig.ShowTools[i - 1] = TAG_APPLE_RECOVERY;
                } else if (MyStriCmp(FlagName, L"windows_recovery")) {
                   GlobalConfig.ShowTools[i - 1] = TAG_WINDOWS_RECOVERY;
                } else if (MyStriCmp(FlagName, L"mok_tool")) {
                   GlobalConfig.ShowTools[i - 1] = TAG_MOK_TOOL;
                } else if (MyStriCmp(FlagName, L"fwupdate")) {
                   GlobalConfig.ShowTools[i - 1] = TAG_FWUPDATE_TOOL;
                } else if (MyStriCmp(FlagName, L"csr_rotate")) {
                   GlobalConfig.ShowTools[i - 1] = TAG_CSR_ROTATE;
                } else if (MyStriCmp(FlagName, L"firmware")) {
                   GlobalConfig.ShowTools[i - 1] = TAG_FIRMWARE;
                } else if (MyStriCmp(FlagName, L"memtest86") || MyStriCmp(FlagName, L"memtest")) {
                   GlobalConfig.ShowTools[i - 1] = TAG_MEMTEST;
                } else if (MyStriCmp(FlagName, L"netboot")) {
                   GlobalConfig.ShowTools[i - 1] = TAG_NETBOOT;
                } else if (MyStriCmp(FlagName, L"hidden_tags")) {
                    GlobalConfig.ShowTools[i - 1] = TAG_HIDDEN;
                    GlobalConfig.HiddenTags = TRUE;
                } else {
                   Print(L" unknown showtools flag: '%s'\n", FlagName);
                }
            }  

        } else if (MyStriCmp(TokenList[0], L"support_gzipped_loaders")) {
           GlobalConfig.GzippedLoaders = HandleBoolean(TokenList, TokenCount);

        } else if (MyStriCmp(TokenList[0], L"theme_background")) {
            HandleString(TokenList, TokenCount, &(GlobalConfig.ThemeBackgroundFileName));
            CleanUpPathNameSlashes(GlobalConfig.ThemeBackgroundFileName);

        } else if (MyStriCmp(TokenList[0], L"theme_logo")) {
            HandleString(TokenList, TokenCount, &(GlobalConfig.ThemeLogoFileName));
            CleanUpPathNameSlashes(GlobalConfig.ThemeLogoFileName);

        } else if (MyStriCmp(TokenList[0], L"theme_mark")) {
            HandleString(TokenList, TokenCount, &(GlobalConfig.ThemeMarkFileName));
            CleanUpPathNameSlashes(GlobalConfig.ThemeMarkFileName);

        } else if (MyStriCmp(TokenList[0], L"theme_underline")) {
            HandleString(TokenList, TokenCount, &(GlobalConfig.ThemeUnderlineFileName));
            CleanUpPathNameSlashes(GlobalConfig.ThemeUnderlineFileName);

        } else if (MyStriCmp(TokenList[0], L"banner")) {
            HandleString(TokenList, TokenCount, &(GlobalConfig.BannerFileName));

        } else if (MyStriCmp(TokenList[0], L"banner_scale") && (TokenCount == 2)) {
            if (MyStriCmp(TokenList[1], L"noscale")) {
                GlobalConfig.BannerScale = BANNER_NOSCALE;
            } else if (MyStriCmp(TokenList[1], L"fillscreen") || MyStriCmp(TokenList[1], L"fullscreen")) {
                GlobalConfig.BannerScale = BANNER_FILLSCREEN;
            } else {
                Print(L" unknown banner_type flag: '%s'\n", TokenList[1]);
            }  

        } else if (MyStriCmp(TokenList[0], L"small_icon_size") && (TokenCount == 2)) {
            HandleInt(TokenList, TokenCount, &i);
            if (i >= 32) {
                GlobalConfig.IconSizes[ICON_SIZE_SMALL] = i;
                HaveResized = TRUE;
            }

        } else if (MyStriCmp(TokenList[0], L"big_icon_size") && (TokenCount == 2)) {
            HandleInt(TokenList, TokenCount, &i);
            if (i >= 32) {
                GlobalConfig.IconSizes[ICON_SIZE_BIG] = i;
                GlobalConfig.IconSizes[ICON_SIZE_BADGE] = i / 4;
                HaveResized = TRUE;
            }

        } else if (MyStriCmp(TokenList[0], L"mouse_size") && (TokenCount == 2)) {
            HandleInt(TokenList, TokenCount, &i);
            if (i >= DEFAULT_MOUSE_SIZE) {
                GlobalConfig.IconSizes[ICON_SIZE_MOUSE] = i;
            }

        } else if (MyStriCmp(TokenList[0], L"selection_small")) {
            HandleString(TokenList, TokenCount, &(GlobalConfig.SelectionSmallFileName));

        } else if (MyStriCmp(TokenList[0], L"selection_big")) {
            HandleString(TokenList, TokenCount, &(GlobalConfig.SelectionBigFileName));

        } else if (MyStriCmp(TokenList[0], L"default_selection")) {
            if (TokenCount == 4) {
                SetDefaultByTime(TokenList, &(GlobalConfig.DefaultSelection));
            } else {
                HandleString(TokenList, TokenCount, &(GlobalConfig.DefaultSelection));
            }

        } else if (MyStriCmp(TokenList[0], L"font_body") && TokenCount == 2) {
            egSelectFontRole(MB_FONT_BODY, TokenList[1]);
        } else if (MyStriCmp(TokenList[0], L"font_bold") && TokenCount == 2) {
            egSelectFontRole(MB_FONT_BOLD, TokenList[1]);
        } else if (MyStriCmp(TokenList[0], L"font_title") && TokenCount == 2) {
            egSelectFontRole(MB_FONT_TITLE, TokenList[1]);

        } else if (MyStriCmp(TokenList[0], L"textonly")) {
            GlobalConfig.TextOnly = HandleBoolean(TokenList, TokenCount);

        } else if (MyStriCmp(TokenList[0], L"textmode")) {
            HandleInt(TokenList, TokenCount, &(GlobalConfig.RequestedTextMode));

        } else if (MyStriCmp(TokenList[0], L"resolution") && ((TokenCount == 2) || (TokenCount == 3))) {
            if (MyStriCmp(TokenList[1], L"max")) {
                GlobalConfig.RequestedScreenWidth = MAX_RES_CODE;
                GlobalConfig.RequestedScreenHeight = MAX_RES_CODE;
            } else {
                GlobalConfig.RequestedScreenWidth = Atoi(TokenList[1]);
                if (TokenCount == 3)
                    GlobalConfig.RequestedScreenHeight = Atoi(TokenList[2]);
                else
                    GlobalConfig.RequestedScreenHeight = 0;
            }

        } else if (MyStriCmp(TokenList[0], L"screensaver")) {
            HandleInt(TokenList, TokenCount, &(GlobalConfig.ScreensaverTime));

        } else if (MyStriCmp(TokenList[0], L"use_graphics_for")) {
            if ((TokenCount == 2) || ((TokenCount > 2) && (!MyStriCmp(TokenList[1], L"+"))))
                GlobalConfig.GraphicsFor = 0;
            for (i = 1; i < TokenCount; i++) {
                if (MyStriCmp(TokenList[i], L"osx")) {
                    GlobalConfig.GraphicsFor |= GRAPHICS_FOR_OSX;
                } else if (MyStriCmp(TokenList[i], L"linux")) {
                    GlobalConfig.GraphicsFor |= GRAPHICS_FOR_LINUX;
                } else if (MyStriCmp(TokenList[i], L"elilo")) {
                    GlobalConfig.GraphicsFor |= GRAPHICS_FOR_ELILO;
                } else if (MyStriCmp(TokenList[i], L"grub")) {
                    GlobalConfig.GraphicsFor |= GRAPHICS_FOR_GRUB;
                } else if (MyStriCmp(TokenList[i], L"windows")) {
                    GlobalConfig.GraphicsFor |= GRAPHICS_FOR_WINDOWS;
                }
            }  


        } else if (MyStriCmp(TokenList[0], L"scan_all_linux_kernels")) {
            GlobalConfig.ScanAllLinux = HandleBoolean(TokenList, TokenCount);

        } else if (MyStriCmp(TokenList[0], L"fold_linux_kernels")) {
            GlobalConfig.FoldLinuxKernels = HandleBoolean(TokenList, TokenCount);

        } else if (MyStriCmp(TokenList[0], L"linux_prefixes")) {
            HandleStrings(TokenList, TokenCount, &(GlobalConfig.LinuxPrefixes));

        } else if (MyStriCmp(TokenList[0], L"extra_kernel_version_strings")) {
            HandleStrings(TokenList, TokenCount, &(GlobalConfig.ExtraKernelVersionStrings));

        } else if (MyStriCmp(TokenList[0], L"max_tags")) {
            HandleInt(TokenList, TokenCount, &(GlobalConfig.MaxTags));

        } else if (MyStriCmp(TokenList[0], L"enable_and_lock_vmx")) {
            GlobalConfig.EnableAndLockVMX = HandleBoolean(TokenList, TokenCount);

        } else if (MyStriCmp(TokenList[0], L"spoof_osx_version")) {
            HandleString(TokenList, TokenCount, &(GlobalConfig.SpoofOSXVersion));

        } else if (MyStriCmp(TokenList[0], L"csr_values")) {
            HandleHexes(TokenList, TokenCount, CSR_MAX_LEGAL_VALUE, &(GlobalConfig.CsrValues));

        } else if (MyStriCmp(TokenList[0], L"write_systemd_vars")) {
            GlobalConfig.WriteSystemdVars = HandleBoolean(TokenList, TokenCount);

        } else if (MyStriCmp(TokenList[0], L"include") && (TokenCount == 2) &&
                   MyStriCmp(FileName, GlobalConfig.ConfigFilename)) {
            if (!MyStriCmp(TokenList[1], FileName)) {
                ReadConfig(TokenList[1]);
            }

        } else if (MyStriCmp(TokenList[0], L"enable_mouse")) {
            GlobalConfig.EnableMouse = HandleBoolean(TokenList, TokenCount);
            if (GlobalConfig.EnableMouse) {
                GlobalConfig.EnableTouch = FALSE;
            }

        } else if (MyStriCmp(TokenList[0], L"enable_touch")) {
            GlobalConfig.EnableTouch = HandleBoolean(TokenList, TokenCount);
            if (GlobalConfig.EnableTouch) {
                GlobalConfig.EnableMouse = FALSE;
            }

        } else if (MyStriCmp(TokenList[0], L"mouse_speed") && (TokenCount == 2)) {
            HandleInt(TokenList, TokenCount, &i);
            if (i < 1)
                i = 1;
            if (i > 32)
                i = 32;
            GlobalConfig.MouseSpeed = i;
        }

        FreeTokenLine(&TokenList, &TokenCount);
    }
    if ((GlobalConfig.DontScanFiles) && (GlobalConfig.WindowsRecoveryFiles))
        MergeStrings(&(GlobalConfig.DontScanFiles), GlobalConfig.WindowsRecoveryFiles, L',');
    MyFreePool(File.Buffer);

    SetLinuxMatchPatterns(GlobalConfig.LinuxPrefixes);

    if (!FileExists(SelfDir, L"icons") && !FileExists(SelfDir, GlobalConfig.IconsDir)) {
        Print(L"Icons directory doesn't exist; setting textonly = TRUE!\n");
        GlobalConfig.TextOnly = TRUE;
    }
}  

/**
 * Start from the parent loader's defaults and parse from the caller's current
 * file cursor through the next closing brace. An empty options or initrd
 * directive clears the inherited value. At completion any remaining initrd path
 * is folded into LoadOptions, and an enabled entry is attached to the submenu.
 */
static VOID AddSubmenu(LOADER_ENTRY *Entry, BOOT_FILE *File, BOOT_VOLUME *Volume, CHAR16 *Title) {
    BOOT_MENU_SCREEN  *SubScreen;
    LOADER_ENTRY       *SubEntry;
    UINTN              TokenCount;
    CHAR16             **TokenList;

    SubScreen = InitializeSubScreen(Entry);

    SubEntry = InitializeLoaderEntry(Entry);

    if ((SubEntry == NULL) || (SubScreen == NULL))
        return;
    SubEntry->me.Title        = StrDuplicate(Title);

    while (((TokenCount = ReadTokenLine(File, &TokenList)) > 0) && (StrCmp(TokenList[0], L"}") != 0)) {

        if (MyStriCmp(TokenList[0], L"loader") && (TokenCount > 1)) {  
            MyFreePool(SubEntry->LoaderPath);
            SubEntry->LoaderPath = StrDuplicate(TokenList[1]);
            SubEntry->Volume = Volume;

        } else if (MyStriCmp(TokenList[0], L"volume") && (TokenCount > 1)) {
            if (FindVolume(&Volume, TokenList[1])) {
                if ((Volume != NULL) && (Volume->IsReadable) && (Volume->RootDir)) {
                    MyFreePool(SubEntry->me.Title);
                    SubEntry->me.Title      = PoolPrint(L"Boot %s from %s",
                                                        (Title != NULL) ? Title : L"Unknown",
                                                        Volume->VolName);
                    SubEntry->me.BadgeImage = Volume->VolBadgeImage;
                    SubEntry->Volume        = Volume;
                }  
            }  

        } else if (MyStriCmp(TokenList[0], L"initrd")) {
            MyFreePool(SubEntry->InitrdPath);
            SubEntry->InitrdPath = NULL;
            if (TokenCount > 1) {
                SubEntry->InitrdPath = StrDuplicate(TokenList[1]);
            }

        } else if (MyStriCmp(TokenList[0], L"options")) {
            MyFreePool(SubEntry->LoadOptions);
            SubEntry->LoadOptions = NULL;
            if (TokenCount > 1) {
                SubEntry->LoadOptions = StrDuplicate(TokenList[1]);
            }  

        } else if (MyStriCmp(TokenList[0], L"add_options") && (TokenCount > 1)) {
            MergeStrings(&SubEntry->LoadOptions, TokenList[1], L' ');

        } else if (MyStriCmp(TokenList[0], L"graphics") && (TokenCount > 1)) {
            SubEntry->UseGraphicsMode = MyStriCmp(TokenList[1], L"on");

        } else if (MyStriCmp(TokenList[0], L"disabled")) {
            SubEntry->Enabled = FALSE;
        }  

        FreeTokenLine(&TokenList, &TokenCount);
    }  

    if (SubEntry->InitrdPath != NULL) {
        MergeStrings(&SubEntry->LoadOptions, L"initrd=", L' ');
        MergeStrings(&SubEntry->LoadOptions, SubEntry->InitrdPath, 0);
        MyFreePool(SubEntry->InitrdPath);
        SubEntry->InitrdPath = NULL;
    }  
    if (SubEntry->Enabled == TRUE) {
        AddMenuEntry(SubScreen, (BOOT_MENU_ENTRY *)SubEntry);
    }
    Entry->me.SubScreen = SubScreen;
}  

/**
 * Consume one menuentry body sequentially. A loader directive derives defaults
 * from the then-current volume and clears inferred options so later options can
 * replace them. A successful volume lookup updates the title, badge and loader
 * volume. Nested submenu parsing advances the same cursor.
 *
 * A firmware_bootnum stanza stores a numeric Boot#### target and sets the
 * firmware dispatch tag. Ordinary file stanzas retain a volume and loader path.
 * This distinction determines whether selecting the entry starts an image or
 * requests a firmware reboot.
 */
static LOADER_ENTRY * AddStanzaEntries(BOOT_FILE *File, BOOT_VOLUME *Volume, CHAR16 *Title) {
    CHAR16       **TokenList;
    UINTN        TokenCount;
    LOADER_ENTRY *Entry;
    BOOLEAN      DefaultsSet = FALSE, AddedSubmenu = FALSE;
    BOOT_VOLUME *CurrentVolume = Volume, *PreviousVolume;

    Entry = InitializeLoaderEntry(NULL);
    if (Entry == NULL)
        return NULL;

    Entry->Title           = StrDuplicate(Title);
    Entry->me.Title        = AllocateZeroPool(256 * sizeof(CHAR16));
    Entry->me.Title        = PoolPrint(L"Boot %s from %s",
                                       (Title != NULL) ? Title : L"Unknown",
                                       CurrentVolume->VolName);
    Entry->me.Row          = 0;
    Entry->me.BadgeImage   = CurrentVolume->VolBadgeImage;
    Entry->Volume          = CurrentVolume;
    Entry->DiscoveryType   = DISCOVERY_TYPE_MANUAL;

    while (((TokenCount = ReadTokenLine(File, &TokenList)) > 0) && (StrCmp(TokenList[0], L"}") != 0)) {
        if (MyStriCmp(TokenList[0], L"loader") && (TokenCount > 1)) {  
            Entry->LoaderPath = StrDuplicate(TokenList[1]);
            LOG(1, LOG_LINE_NORMAL, L"Adding manual loader for '%s'", Entry->LoaderPath);
            SetLoaderDefaults(Entry, TokenList[1], CurrentVolume);
            MyFreePool(Entry->LoadOptions);
            Entry->LoadOptions = NULL;  
            DefaultsSet = TRUE;

        } else if (MyStriCmp(TokenList[0], L"volume") && (TokenCount > 1)) {
            PreviousVolume = CurrentVolume;
            if (FindVolume(&CurrentVolume, TokenList[1])) {
                if ((CurrentVolume != NULL) && (CurrentVolume->IsReadable) && (CurrentVolume->RootDir)) {
                    MyFreePool(Entry->me.Title);
                    Entry->me.Title        = PoolPrint(L"Boot %s from %s",
                                                       (Title != NULL) ? Title : L"Unknown",
                                                       CurrentVolume->VolName);
                    Entry->me.BadgeImage   = CurrentVolume->VolBadgeImage;
                    Entry->Volume          = CurrentVolume;
                } else {  
                    CurrentVolume = PreviousVolume;
                }  
            }  

        } else if (MyStriCmp(TokenList[0], L"icon") && (TokenCount > 1)) {
            MyFreePool(Entry->me.Image);
            Entry->me.Image = egLoadIcon(CurrentVolume->RootDir, TokenList[1], GlobalConfig.IconSizes[ICON_SIZE_BIG]);
            if (Entry->me.Image == NULL) {
                Entry->me.Image = DummyImage(GlobalConfig.IconSizes[ICON_SIZE_BIG]);
            }

        } else if (MyStriCmp(TokenList[0], L"initrd") && (TokenCount > 1)) {
            MyFreePool(Entry->InitrdPath);
            Entry->InitrdPath = StrDuplicate(TokenList[1]);

        } else if (MyStriCmp(TokenList[0], L"options") && (TokenCount > 1)) {
            MyFreePool(Entry->LoadOptions);
            Entry->LoadOptions = StrDuplicate(TokenList[1]);

        } else if (MyStriCmp(TokenList[0], L"ostype") && (TokenCount > 1)) {
            if (TokenCount > 1) {
                Entry->OSType = TokenList[1][0];
            }

        } else if (MyStriCmp(TokenList[0], L"graphics") && (TokenCount > 1)) {
            Entry->UseGraphicsMode = MyStriCmp(TokenList[1], L"on");

        } else if (MyStriCmp(TokenList[0], L"disabled")) {
            LOG(1, LOG_LINE_NORMAL, L"Entry is disabled");
            Entry->Enabled = FALSE;

        } else if (MyStriCmp(TokenList[0], L"firmware_bootnum") && (TokenCount > 1)) {
            Entry->EfiBootNum = StrToHex(TokenList[1], 0, 16);
            Entry->EfiLoaderPath = NULL;
            Entry->LoaderPath = NULL;
            MyFreePool(Entry->me.Title);
            Entry->me.Title = StrDuplicate(Title);
            Entry->me.BadgeImage = BuiltinIcon(BUILTIN_ICON_VOL_EFI);
            Entry->me.Tag = TAG_FIRMWARE_LOADER;

        } else if (MyStriCmp(TokenList[0], L"submenuentry") && (TokenCount > 1)) {
            AddSubmenu(Entry, File, CurrentVolume, TokenList[1]);
            AddedSubmenu = TRUE;

        }  
        FreeTokenLine(&TokenList, &TokenCount);
    }  

    if (AddedSubmenu)
         AddMenuEntry(Entry->me.SubScreen, &MenuEntryReturn);

    if (Entry->InitrdPath) {
        MergeStrings(&Entry->LoadOptions, L"initrd=", L' ');
        MergeStrings(&Entry->LoadOptions, Entry->InitrdPath, 0);
        MyFreePool(Entry->InitrdPath);
        Entry->InitrdPath = NULL;
    }  

    if (!DefaultsSet) {

        SetLoaderDefaults(Entry, L"\\EFI\\BOOT\\nemo.efi", CurrentVolume);
    }

    return(Entry);
}  

/**
 * Reopen the configuration for the manual discovery pass. Only enabled stanzas
 * are published to MainMenu. Generate a subscreen when the stanza supplied none.
 * Includes are followed from the main filename only and a self-include is
 * ignored, bounding the include depth.
 */
VOID ScanUserConfigured(CHAR16 *FileName)
{
    EFI_STATUS        Status;
    BOOT_FILE        File;
    BOOT_VOLUME      *Volume;
    CHAR16            **TokenList;
    UINTN             TokenCount, size;
    LOADER_ENTRY      *Entry;

    LOG(1, LOG_LINE_THIN_SEP, L"Scanning for user-configured boot stanzas");
    if (FileExists(SelfDir, FileName)) {
        Status = ReadFile(SelfDir, FileName, &File, &size);
        if (EFI_ERROR(Status))
            return;

        Volume = SelfVolume;

        while ((TokenCount = ReadTokenLine(&File, &TokenList)) > 0) {
            if (MyStriCmp(TokenList[0], L"menuentry") && (TokenCount > 1)) {
                Entry = AddStanzaEntries(&File, Volume, TokenList[1]);
                if ((Entry) && (Entry->Enabled)) {
                    if (Entry->me.SubScreen == NULL)
                        GenerateSubScreen(Entry, Volume, TRUE);
                    AddPreparedLoaderEntry(Entry);
                } else {
                    MyFreePool(Entry);
                }  

            } else if (MyStriCmp(TokenList[0], L"include") && (TokenCount == 2) &&
                       MyStriCmp(FileName, GlobalConfig.ConfigFilename)) {
                if (!MyStriCmp(TokenList[1], FileName)) {
                    ScanUserConfigured(TokenList[1]);
                }

            }  
            FreeTokenLine(&TokenList, &TokenCount);
        }  
    }  
}  

/**
 * Recognise both a plain device path and a device specification split at "=" by
 * ReadTokenLine. The root mount point arrives as a backslash because tokenisation
 * normalises unquoted slashes. Convert the device string back to Linux separators
 * before synthesising normal and single-user options.
 */
static BOOT_FILE * GenerateOptionsFromEtcFstab(BOOT_VOLUME *Volume) {
    UINTN        TokenCount, i;
    BOOT_FILE   *Options = NULL, *Fstab = NULL;
    EFI_STATUS   Status;
    CHAR16       **TokenList, *Line, *Root = NULL;

    if (FileExists(Volume->RootDir, L"\\etc\\fstab")) {
        Options = AllocateZeroPool(sizeof(BOOT_FILE));
        Fstab = AllocateZeroPool(sizeof(BOOT_FILE));
        Status = ReadFile(Volume->RootDir, L"\\etc\\fstab", Fstab, &i);
        if (CheckError(Status, L"while reading /etc/fstab")) {
            if (Options != NULL)
                MyFreePool(Options);
            if (Fstab != NULL)
                MyFreePool(Fstab);
            Options = NULL;
            Fstab = NULL;
        } else {  
            Options->Encoding = ENCODING_UTF16_LE;
            while ((TokenCount = ReadTokenLine(Fstab, &TokenList)) > 0) {
                LOG(3, LOG_LINE_NORMAL, L"Read line from /etc/fstab holding %d tokens", TokenCount);
                if (TokenCount > 2) {
                    if (StrCmp(TokenList[1], L"\\") == 0) {
                        Root = PoolPrint(L"%s", TokenList[0]);
                    } else if (StrCmp(TokenList[2], L"\\") == 0) {
                        Root = PoolPrint(L"%s=%s", TokenList[0], TokenList[1]);
                    }  
                    if (Root && (Root[0] != L'\0')) {
                        for (i = 0; i < StrLen(Root); i++)
                            if (Root[i] == '\\')
                                Root[i] = '/';
                        Line = PoolPrint(L"\"Boot with normal options\"    \"ro root=%s quiet splash\"\n", Root);
                        MergeStrings((CHAR16 **) &(Options->Buffer), Line, 0);
                        MyFreePool(Line);
                        Line = PoolPrint(L"\"Boot into single-user mode\"  \"ro root=%s single\"\n", Root);
                        MergeStrings((CHAR16**) &(Options->Buffer), Line, 0);
                        MyFreePool(Line);
                        Options->BufferSize = StrLen((CHAR16*) Options->Buffer) * sizeof(CHAR16);
                    }  
                    MyFreePool(Root);
                    Root = NULL;
                 }  
                 FreeTokenLine(&TokenList, &TokenCount);
            }  

            if (Options->Buffer) {
                Options->Current8Ptr  = (CHAR8 *)Options->Buffer;
                Options->End8Ptr      = Options->Current8Ptr + Options->BufferSize;
                Options->Current16Ptr = (CHAR16 *)Options->Buffer;
                Options->End16Ptr     = Options->Current16Ptr + (Options->BufferSize >> 1);
            } else {
                MyFreePool(Options);
                Options = NULL;
            }

            MyFreePool(Fstab->Buffer);
            MyFreePool(Fstab);
        }  
    }  
    return Options;
}  

/**
 * Use the discovered root partition's unique PartGuid to form
 * /dev/disk/by-partuuid/<guid>. Its GPT read-only attribute selects ro or rw. The
 * generated CHAR16 text has the same label/options pairs as an on-disk Linux
 * options file.
 */
static BOOT_FILE * GenerateOptionsFromPartTypes(VOID) {
    BOOT_FILE   *Options = NULL;
    CHAR16       *Line, *GuidString, *WriteStatus;

    if (GlobalConfig.DiscoveredRoot) {
        Options = AllocateZeroPool(sizeof(BOOT_FILE));
        if (Options) {
            Options->Encoding = ENCODING_UTF16_LE;
            GuidString = GuidAsString(&(GlobalConfig.DiscoveredRoot->PartGuid));
            WriteStatus = GlobalConfig.DiscoveredRoot->IsMarkedReadOnly ? L"ro" : L"rw";
            ToLower(GuidString);
            if (GuidString) {
                Line = PoolPrint(L"\"Boot with normal options\"    \"%s root=/dev/disk/by-partuuid/%s quiet splash\"\n",
                                 WriteStatus, GuidString);
                MergeStrings((CHAR16 **) &(Options->Buffer), Line, 0);
                MyFreePool(Line);
                Line = PoolPrint(L"\"Boot into single-user mode\"  \"%s root=/dev/disk/by-partuuid/%s single\"\n",
                                 WriteStatus, GuidString);
                MergeStrings((CHAR16**) &(Options->Buffer), Line, 0);
                MyFreePool(Line);
                MyFreePool(GuidString);
            }  
            Options->BufferSize = StrLen((CHAR16*) Options->Buffer) * sizeof(CHAR16);

            Options->Current8Ptr  = (CHAR8 *)Options->Buffer;
            Options->End8Ptr      = Options->Current8Ptr + Options->BufferSize;
            Options->Current16Ptr = (CHAR16 *)Options->Buffer;
            Options->End16Ptr     = Options->Current16Ptr + (Options->BufferSize >> 1);
        }  
    }  
    return Options;
}  

/*
 * Synthetic Linux fallback entries use quiet splash for a normal graphical
 * startup when a distribution has not provided an explicit kernel options
 * file. Recovery/single-user entries remain verbose. Existing options files
 * are authoritative and are never rewritten by this fallback.
 */
/**
 * Search the kernel's own directory in LINUX_OPTIONS_FILENAMES order and stop on
 * the first successful read. If none opens, try the volume's fstab and then the
 * partition-type discovery result. Both synthetic sources use the same BOOT_FILE
 * cursor contract as a physical file.
 */
BOOT_FILE * ReadLinuxOptionsFile(IN CHAR16 *LoaderPath, IN BOOT_VOLUME *Volume) {
    CHAR16       *OptionsFilename, *FullFilename;
    BOOLEAN      GoOn = TRUE, FileFound = FALSE;
    UINTN        i = 0, size;
    BOOT_FILE   *File = NULL;
    EFI_STATUS   Status;

    do {
        OptionsFilename = FindCommaDelimited(LINUX_OPTIONS_FILENAMES, i++);
        FullFilename = FindPath(LoaderPath);
        if ((OptionsFilename != NULL) && (FullFilename != NULL)) {
            MergeStrings(&FullFilename, OptionsFilename, '\\');
            if (FileExists(Volume->RootDir, FullFilename)) {
                File = AllocateZeroPool(sizeof(BOOT_FILE));
                Status = ReadFile(Volume->RootDir, FullFilename, File, &size);
                if (CheckError(Status, L"while loading the Linux options file")) {
                    MyFreePool(File);
                    File = NULL;
                } else {
                    GoOn = FALSE;
                    FileFound = TRUE;
                }  
            }  
        } else {  
            GoOn = FALSE;
        }  
        MyFreePool(OptionsFilename);
        MyFreePool(FullFilename);
        OptionsFilename = FullFilename = NULL;
    } while (GoOn);
    if (!FileFound) {
        /*
         * The first readable options file beside the kernel wins. Only when
         * none was read does BOOT synthesise options from this volume's fstab
         * and then, if necessary, the discovered partition type.
         */
        File = GenerateOptionsFromEtcFstab(Volume);

        if (!File)
            File = GenerateOptionsFromPartTypes();
    }  
    return (File);
}  

/**
 * Read just the first token-bearing line and duplicate its second field for the
 * primary boot entry. Further option lines belong to the submenu path. Missing
 * files and a first line lacking a second field both yield NULL.
 *
 * The returned CHAR16 option string is a new pool allocation owned by the
 * caller. ReadLinuxOptionsFile returns a BOOT_FILE whose payload may itself
 * be pool allocated. This routine frees the BOOT_FILE wrapper but does not
 * explicitly release File->Buffer; the ownership/lifetime of that payload
 * therefore requires a separate audit. A non-NULL command line is an options
 * selection, not evidence that the nominated root or initrd can be mounted.
 * The absence of an explicit File->Buffer release is a possible ownership
 * leak, not an intentional transfer to the caller. Verify this against the
 * file-reader allocation contract before claiming the entire path leak-free.
 */
CHAR16 * GetFirstOptionsFromFile(IN CHAR16 *LoaderPath, IN BOOT_VOLUME *Volume) {
    UINTN        TokenCount;
    CHAR16       *Options = NULL;
    CHAR16       **TokenList;
    BOOT_FILE   *File;

    File = ReadLinuxOptionsFile(LoaderPath, Volume);
    if (File != NULL) {
        TokenCount = ReadTokenLine(File, &TokenList);
        if (TokenCount > 1)
            Options = StrDuplicate(TokenList[1]);
        FreeTokenLine(&TokenList, &TokenCount);
        MyFreePool(File);
    }  
    return Options;
}  
