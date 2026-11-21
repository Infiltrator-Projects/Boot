/*
 * Small blob-decompression API consuming caller-provided destination space.
 * Return code and resulting size are required before treating output as data.
 */

#ifndef _INFUTIL_H
#define _INFUTIL_H

#include "zlib.h"

/*
 * Keep inflate_state first so the caller can supply just that prefix in the
 * contiguous-output profile. Streaming callers also need working_window to
 * retain history after an output buffer is flushed.
 */
struct inflate_workspace {
    struct inflate_state inflate_state;
#ifdef CONFIG_ZLIB_DFLTCC
    struct dfltcc_state dfltcc_state;
    unsigned char working_window[(1 << MAX_WBITS) + PAGE_SIZE];
#else
    unsigned char working_window[(1 << MAX_WBITS)];
#endif
};

#ifdef CONFIG_ZLIB_DFLTCC

static_assert(offsetof(struct inflate_workspace, dfltcc_state) % 8 == 0);
#endif

#define WS(strm) ((struct inflate_workspace *)(strm->workspace))

#endif
