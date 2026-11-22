/* UEFI diagnostic logging and file rotation.
 * Files are opened through firmware protocols; log I/O is best-effort
 * and never substitutes for the EFI_STATUS of the logged operation.
 */

#include "log.h"
#include "global.h"
#include "install.h"
#include "lib.h"
#include "mystrings.h"
#include "../include/refit_call_wrapper.h"
#include "screen.h"
#include "menu.h"

EFI_FILE_HANDLE  gLogHandle;
CHAR16           *gLogTemp = NULL;
BOOLEAN          gLogActive = FALSE;
static UINTN     BootDiagPendingWrites;

/*
 * BOOT only needs approximate millisecond resolution to distinguish a
 * 14-second firmware controller call from a 45-second one. On x86 we
 * calibrate the CPU timestamp counter against UEFI Stall once per boot;
 * other architectures fall back to firmware time (which may be coarse).
 * No operating-system timer, Linux service or driver is involved.
 */
static UINT64 BootDiagTicksPerMs;

#define BOOT_DIAG_EARLY_VOLUME_CAP 64
#define BOOT_DIAG_VOLUME_NAME_CAP 72
typedef struct {
    UINTN Index, Total;
    UINT64 Milliseconds;
    CHAR16 Name[BOOT_DIAG_VOLUME_NAME_CAP];
} BOOT_EARLY_VOLUME_RECORD;
static BOOT_EARLY_VOLUME_RECORD BootDiagEarlyVolumes[BOOT_DIAG_EARLY_VOLUME_CAP];
static UINTN BootDiagEarlyCount;
static UINTN BootDiagEarlyDropped;
static BOOLEAN BootDiagEarlyEnumerationPresent;
static UINT64 BootDiagEarlyEnumerationMs;
static UINTN BootDiagEarlyEnumerationCount;
static EFI_STATUS BootDiagEarlyEnumerationStatus;


/* Open one named EFI file for read/write and invoke Delete on that handle.
 * UEFI Delete consumes the opened handle; unlike Closing, it mutates the
 * filesystem. Rotation uses this only for BOOT's own log paths, and a
 * successful open does not imply that media permits deletion.
 */
EFI_STATUS DeleteFile(IN EFI_FILE_PROTOCOL *BaseDir, CHAR16 *FileName) {
    EFI_FILE_HANDLE FileHandle;
    UINT64          FileMode = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE;;
    EFI_STATUS      Status;

    Status = refit_call5_wrapper(BaseDir->Open, BaseDir, &FileHandle,
                                 FileName, FileMode, 0);
    if (!EFI_ERROR(Status)) {
        Status = refit_call1_wrapper(FileHandle->Delete, FileHandle);
    }
    return Status;
}  

/*
 * Make room for the new session by discarding the old backup and attempting
 * to rename the current log. If backup fails, delete the current log and
 * propagate the deletion status.
 */
EFI_STATUS RotateLogFile(EFI_FILE_HANDLE Location) {
    EFI_STATUS Status = EFI_SUCCESS;

    if (FileExists(Location, LOGFILE)) {
        if (FileExists(Location, LOGFILE_OLD))
            DeleteFile(Location, LOGFILE_OLD);
        Status = BackupOldFile(Location, LOGFILE);
        if (EFI_ERROR(Status)) {
            Status = DeleteFile(Location, LOGFILE);
        }
    }
    return Status;
}  

/**
 * A fresh session rotates the old log and writes a UTF-16LE byte-order mark to an
 * empty file. A restart reopens the existing log and seeks to the firmware
 * end-of-file sentinel. Both paths first try SelfDir and then the ESP if opening
 * there fails.
 */
EFI_STATUS StartLogging(BOOLEAN Restart) {
    EFI_STATUS      Status = EFI_SUCCESS;
    UINT64          FileMode;
    UINTN           BufferSize;
    UINTN           gcLogLevel;
    EFI_FILE_HANDLE FoundEsp;
    EFI_FILE_INFO   *FileInfo;
    UINT8           Utf16[2];  

    if (GlobalConfig.LogLevel > 0) {

        gcLogLevel = GlobalConfig.LogLevel;
        GlobalConfig.LogLevel = 0;
        if (Restart) {
            FileMode = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE;
        } else {
            FileMode = EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE;
            Status = RotateLogFile(SelfDir);
        }
        if (Status == EFI_SUCCESS) {
            Status = refit_call5_wrapper(SelfDir->Open, SelfDir, &gLogHandle, LOGFILE,
                                         FileMode, 0);
        }

        if (EFI_ERROR(Status)) {

            Status = egFindESP(&FoundEsp);
            if (!EFI_ERROR(Status)) {
                if (!Restart) {
                    Status = RotateLogFile(FoundEsp);
                }
                if (Status == EFI_SUCCESS)
                    Status = refit_call5_wrapper(FoundEsp->Open, FoundEsp,
                                                 &gLogHandle, LOGFILE,
                                                 FileMode, 0);
            }
        }
        if (EFI_ERROR(Status)) {
            gcLogLevel = 0;
            PrintUglyText(L"Unable to open log file!", CENTER);
            PauseForKey();
        } else {

            FileInfo = LibFileInfo(gLogHandle);
            if ((FileInfo != NULL) && (FileInfo->FileSize > 0)) {
                if (!Restart) {
                    Status = refit_call1_wrapper(gLogHandle->Delete, gLogHandle);
                    StartLogging(FALSE);
                }
            } else {

                Utf16[0] = 0xFF;
                Utf16[1] = 0xFE;
                BufferSize = 2;
                refit_call3_wrapper(gLogHandle->Write, gLogHandle, &BufferSize, Utf16);
                gLogActive = TRUE;
                LOG(1, LOG_LINE_SEPARATOR, L"Beginning logging");
            }  
            if (Restart) {
                refit_call2_wrapper(gLogHandle->SetPosition, gLogHandle, 0xFFFFFFFFFFFFFFFF);
            }
            gLogActive = TRUE;
        }  
        GlobalConfig.LogLevel = gcLogLevel;
    }  
    return Status;
}  

/*
 * Ends the active logging session and disposes of its open EFI file handle.
 * Later diagnostic emission must not assume that the prior handle remains
 * valid across child-image dispatch or protocol reinitialisation.
 */
VOID StopLogging(VOID) {
    if (gLogActive)
        BootDiagFlush();
    if (GlobalConfig.LogLevel > 0 && gLogActive)
        refit_call1_wrapper(gLogHandle->Close, gLogHandle);
    gLogActive = FALSE;
}  

/**
 * Consume the caller's allocated message and clear its pointer even when logging
 * is inactive. Writes use the UTF-16 byte length without a terminator and flush
 * immediately, allowing a later boot stage to inspect the last completed write.
 * Write and flush statuses are discarded here.
 */
VOID WriteToLog(CHAR16 **Message, UINTN LogLineType) {
    CHAR16   *TimeStr;
    CHAR16   *FinalMessage = NULL;
    UINTN    BufferSize;

    if (gLogActive) {
        switch (LogLineType) {
            case LOG_LINE_SEPARATOR:
                FinalMessage = PoolPrint(L"\n==========%s==========\n", *Message);
                break;
            case LOG_LINE_THIN_SEP:
                FinalMessage = PoolPrint(L"\n----------%s----------\n", *Message);
                break;
            default:  
                TimeStr = GetTimeString();
                FinalMessage = PoolPrint(L"%s - %s\n", TimeStr, *Message);
                if (TimeStr)
                    FreePool(TimeStr);
                break;
        }  

        if (FinalMessage) {
            BufferSize = StrLen(FinalMessage) * 2;
            refit_call3_wrapper(gLogHandle->Write, gLogHandle, &BufferSize, FinalMessage);
            /*
             * With startup diagnostics enabled, repeated FAT flushes of
             * thousands of small lines obscure firmware timing. Flush
             * separators, every 64 records and explicitly before slow EFI
             * calls instead. When disabled, preserve ordinary log behaviour.
             */
            if (!GlobalConfig.StartupDiagnostics ||
                LogLineType != LOG_LINE_NORMAL ||
                ++BootDiagPendingWrites >= 64) {
                refit_call1_wrapper(gLogHandle->Flush, gLogHandle);
                BootDiagPendingWrites = 0;
            }
            FreePool(FinalMessage);
        }
    }
    if (*Message) {
        FreePool(*Message);
        *Message = NULL;
    }
}  


/* A flushed BEGIN marker survives a later stuck firmware controller call. */
VOID BootDiagFlush(VOID)
{
    if (!gLogActive || !gLogHandle)
        return;
    refit_call1_wrapper(gLogHandle->Flush, gLogHandle);
    BootDiagPendingWrites = 0;
}

/* Raw cycle counter on x86, firmware wall clock otherwise. */
UINT64 BootDiagTicks(VOID)
{
#if defined(EFIX64) || defined(EFI32)
    UINT32 Lower, Upper;
    __asm__ __volatile__("rdtsc" : "=a"(Lower), "=d"(Upper));
    return ((UINT64)Upper << 32) | Lower;
#else
    EFI_TIME Now;
    EFI_STATUS Status;
    Status = refit_call2_wrapper(ST->RuntimeServices->GetTime, &Now, NULL);
    if (EFI_ERROR(Status))
        return 0;
    return ((UINT64)Now.Hour * 3600000ULL) +
           ((UINT64)Now.Minute * 60000ULL) +
           ((UINT64)Now.Second * 1000ULL) +
           ((UINT64)Now.Nanosecond / 1000000ULL);
#endif
}

VOID BootDiagCalibrateClock(VOID)
{
#if defined(EFIX64) || defined(EFI32)
    UINT64 Before, After;
    if (!GlobalConfig.StartupDiagnostics)
        return;
    Before = BootDiagTicks();
    refit_call1_wrapper(BS->Stall, 50000);
    After = BootDiagTicks();
    if (After > Before)
        BootDiagTicksPerMs = (After - Before) / 50;
#endif
}

UINT64 BootDiagElapsedMs(UINT64 Begin)
{
    UINT64 End = BootDiagTicks();
    if (End < Begin)
        return 0;
#if defined(EFIX64) || defined(EFI32)
    if (!BootDiagTicksPerMs)
        return 0;
    return (End - Begin) / BootDiagTicksPerMs;
#else
    return End - Begin;
#endif
}

/*
 * The first scan precedes creation of boot.log. Keep a bounded snapshot of
 * individual volume timings and flush it once the configured log is open.
 * These names are diagnostic labels only and cannot affect volume selection.
 */
VOID BootDiagHandleEnumeration(UINT64 Milliseconds, UINTN Count, EFI_STATUS Status)
{
    if (!GlobalConfig.StartupDiagnostics)
        return;
    if (gLogActive) {
        LOG(1, LOG_LINE_NORMAL,
            L"[DIAG] BlockIo handle enumeration count=%d status=%lu elapsed=%lu ms",
            Count, (UINT64)Status, Milliseconds);
        return;
    }
    BootDiagEarlyEnumerationPresent = TRUE;
    BootDiagEarlyEnumerationMs = Milliseconds;
    BootDiagEarlyEnumerationCount = Count;
    BootDiagEarlyEnumerationStatus = Status;
}

VOID BootDiagVolumeTiming(UINTN Index, UINTN Total, CHAR16 *VolumeName,
                          UINT64 Milliseconds)
{
    BOOT_EARLY_VOLUME_RECORD *Entry;
    UINTN i;
    if (!GlobalConfig.StartupDiagnostics)
        return;
    if (gLogActive) {
        LOG(1, LOG_LINE_NORMAL,
            L"[DIAG] volume %d/%d name='%s' scan=%lu ms",
            Index + 1, Total, VolumeName ? VolumeName : L"(unnamed)",
            Milliseconds);
        return;
    }
    if (BootDiagEarlyCount >= BOOT_DIAG_EARLY_VOLUME_CAP) {
        BootDiagEarlyDropped++;
        return;
    }
    Entry = &BootDiagEarlyVolumes[BootDiagEarlyCount++];
    Entry->Index = Index;
    Entry->Total = Total;
    Entry->Milliseconds = Milliseconds;
    if (!VolumeName)
        VolumeName = L"(unnamed)";
    for (i = 0; i + 1 < BOOT_DIAG_VOLUME_NAME_CAP && VolumeName[i]; i++)
        Entry->Name[i] = VolumeName[i];
    Entry->Name[i] = 0;
}

VOID BootDiagFlushEarlyVolumes(VOID)
{
    UINTN i;
    if (!GlobalConfig.StartupDiagnostics || !gLogActive)
        return;
    if (BootDiagEarlyEnumerationPresent) {
        LOG(1, LOG_LINE_NORMAL,
            L"[DIAG] initial BlockIo handle enumeration count=%d status=%lu elapsed=%lu ms",
            BootDiagEarlyEnumerationCount, (UINT64)BootDiagEarlyEnumerationStatus,
            BootDiagEarlyEnumerationMs);
        BootDiagEarlyEnumerationPresent = FALSE;
    }
    LOG(1, LOG_LINE_NORMAL, L"[DIAG] initial volume scan records=%d dropped=%d",
        BootDiagEarlyCount, BootDiagEarlyDropped);
    for (i = 0; i < BootDiagEarlyCount; ++i) {
        BOOT_EARLY_VOLUME_RECORD *Entry = &BootDiagEarlyVolumes[i];
        LOG(1, LOG_LINE_NORMAL,
            L"[DIAG] initial volume %d/%d name='%s' scan=%lu ms",
            Entry->Index + 1, Entry->Total, Entry->Name, Entry->Milliseconds);
    }
    BootDiagEarlyCount = 0;
    BootDiagEarlyDropped = 0;
    BootDiagFlush();
}
