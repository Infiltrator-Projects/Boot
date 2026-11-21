/*
 * Diagnostic recording interface for BOOT's EFI-volume log. Log write errors
 * must not be interpreted as the status of a loader transfer, and logging
 * should not require destructive operations on other EFI vendors' directories.
 */
 






 








#ifndef __LOG_H_
#define __LOG_H_

#ifdef __MAKEWITH_GNUEFI
#include "efi.h"
#include "efilib.h"
#define EFI_DEVICE_PATH_PROTOCOL EFI_DEVICE_PATH
#else
#include "../include/tiano_includes.h"
#endif

extern CHAR16           *gLogTemp;

#define LOG_LINE_NORMAL      1
#define LOG_LINE_SEPARATOR   2
#define LOG_LINE_THIN_SEP    3

#define LOGFILE L"boot.log"
#define LOGFILE_OLD L"boot.log-old"
/*
 * Formatting happens inside the level check. Arguments with side effects
 * would therefore execute only when that logging level is enabled. WriteToLog
 * receives the address of the shared temporary string.
 */
#define LOG(level, type, ...) \
    if (level <= GlobalConfig.LogLevel) { \
        gLogTemp = PoolPrint(__VA_ARGS__); \
        WriteToLog(&gLogTemp, type); \
    }

EFI_STATUS StartLogging(BOOLEAN Restart);
VOID StopLogging(VOID);
VOID WriteToLog(CHAR16 **Message, UINTN LogLineType);

#endif
