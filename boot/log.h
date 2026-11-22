/* Firmware log levels, output and lifecycle declarations.
 * Log operations may fail independently of the action being traced.
 * Callers retain responsibility for EFI_STATUS handling.
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

/*
 * StartLogging opens BOOT's own diagnostic file, optionally rotating old
 * logs. StopLogging closes the session; WriteToLog consumes a mutable
 * pointer to the shared formatting buffer. Logging errors must not
 * override image-launch status.
 */
EFI_STATUS StartLogging(BOOLEAN Restart);
VOID StopLogging(VOID);
VOID WriteToLog(CHAR16 **Message, UINTN LogLineType);

/* High-resolution diagnostic timing, explicitly gated by startup_diagnostics. */
VOID BootDiagCalibrateClock(VOID);
UINT64 BootDiagTicks(VOID);
UINT64 BootDiagElapsedMs(UINT64 Begin);
VOID BootDiagFlush(VOID);
VOID BootDiagHandleEnumeration(UINT64 Milliseconds, UINTN Count, EFI_STATUS Status);
VOID BootDiagVolumeTiming(UINTN Index, UINTN Total, CHAR16 *VolumeName, UINT64 Milliseconds);
VOID BootDiagFlushEarlyVolumes(VOID);

#endif
