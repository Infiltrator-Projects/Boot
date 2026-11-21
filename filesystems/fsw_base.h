/*
 * Portability foundation for the filesystem wrapper. Integer widths,
 * endianness and allocation/platform hooks must match the actual firmware
 * build profile; no hosted libc or Linux VFS semantics may be presumed.
 */

#ifndef _FSW_BASE_H_
#define _FSW_BASE_H_

#ifdef __MAKEWITH_TIANO
#include "fsw_efi_base.h"
#endif

#ifdef HOST_POSIX
#include "test/fsw_posix_base.h"
#endif

#ifndef FSW_DEBUG_LEVEL

#define FSW_DEBUG_LEVEL 1
#endif
/*
 * The FSW_MSG macros emit diagnostics according to the build's debug level.
 * In particular, a disabled FSW_MSG_ASSERT expands to nothing. Any control
 * flow that rejects bad metadata must be expressed by the calling code.
 */
#if FSW_DEBUG_LEVEL >= 1
#define FSW_MSG_ASSERT(params) FSW_MSGFUNC params
#else
#define FSW_MSG_ASSERT(params)
#endif

#if FSW_DEBUG_LEVEL >= 2
#define FSW_MSG_DEBUG(params) FSW_MSGFUNC params
#else
#define FSW_MSG_DEBUG(params)
#endif

#if FSW_DEBUG_LEVEL >= 3
#define FSW_MSG_DEBUGV(params) FSW_MSGFUNC params
#else
#define FSW_MSG_DEBUGV(params)
#endif

#endif
