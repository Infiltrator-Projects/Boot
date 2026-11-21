/*
 * BOOT's diagnostic log output and rotation within the EFI file protocol.
 * Logging is a side effect on the writable volume; it is not a substitute
 * for propagation of EFI_STATUS and is not proof of successful OS startup.
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
    if (GlobalConfig.LogLevel > 0)
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
            refit_call1_wrapper(gLogHandle->Flush, gLogHandle);
            FreePool(FinalMessage);
        }
    }
    if (*Message) {
        FreePool(*Message);
        *Message = NULL;
    }
}  
