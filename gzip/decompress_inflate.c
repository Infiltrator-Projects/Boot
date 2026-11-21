/*
 * Firmware-facing gzip decompression adaptor for in-memory EFI images.
 * Inflate uses bounded input/output lengths; an apparent gzip header does
 * not validate an EFI executable. Output size must be verified before passing
 * the buffer to LoadImage, and allocation belongs to EFI pool services.
 */

#include "../boot/lib.h"
#include "../libeg/lodepng.h"
#include "../boot/log.h"

#define malloc AllocatePool
#define free MyFreePool

#include "zlib_inflate/inftrees.c"
#include "zlib_inflate/inffast.c"
#include "zlib_inflate/inflate.c"

#define GZIP_IOBUF_SIZE (16*1024)

#define u8 UINT8

static long nofill(void *buffer, unsigned long len)
{
    return -1;
}

/**
 * Decode one raw DEFLATE stream after the gzip wrapper has consumed the fixed
 * header and optional filename. A memory destination supplies capacity. A flush
 * callback instead receives chunks from reusable scratch storage, so its current
 * write pointer cannot express cumulative output.
 *
 * Set decoded_size to zero before any allocation. Publish strm->total_out only at
 * Z_STREAM_END after any pending flush succeeds. A full output buffer, a failed
 * callback or a decoder error must never hand the firmware a partial image
 * length.
 */
int gunzip(unsigned char *buf, long len,
               long (*fill)(void*, unsigned long),
               long (*flush)(void*, unsigned long),
               unsigned char *out_buf, long out_len,
               UINTN *decoded_size) {
    u8 *zbuf;
    struct z_stream_s *strm;
    int rc;

    if (decoded_size)
        *decoded_size = 0;
    rc = -1;
    if (flush) {
        out_len = 0x8000;  
        out_buf = malloc(out_len);
    } else {
        if (!out_len)
            out_len = ((size_t)~0) - (size_t)out_buf;  
    }
    if (!out_buf) {
        LOG(1, LOG_LINE_NORMAL, L"Out of memory while allocating output buffer");
        goto gunzip_nomem1;
    }

    if (buf)
        zbuf = buf;
    else {
        zbuf = malloc(GZIP_IOBUF_SIZE);
        len = 0;
    }
    if (!zbuf) {
        LOG(1, LOG_LINE_NORMAL, L"Out of memory while allocating input buffer");
        goto gunzip_nomem2;
    }

    strm = malloc(sizeof(*strm));
    if (strm == NULL) {
        LOG(1, LOG_LINE_NORMAL, L"Out of memory while allocating z_stream");
        goto gunzip_nomem3;
    }

    strm->workspace = malloc(flush ? zlib_inflate_workspacesize() :
#ifdef CONFIG_ZLIB_DFLTCC

                 zlib_inflate_workspacesize());
#else
                 sizeof(struct inflate_state));
#endif
    if (strm->workspace == NULL) {
        LOG(1, LOG_LINE_NORMAL, L"Out of memory while allocating workspace");
        goto gunzip_nomem4;
    }

    if (!fill)
        fill = nofill;

    if (len == 0)
        len = fill(zbuf, GZIP_IOBUF_SIZE);

    if (len < 10 ||
       zbuf[0] != 0x1f || zbuf[1] != 0x8b || zbuf[2] != 0x08) {
        LOG(1, LOG_LINE_NORMAL, L"Not a gzip file");
        goto gunzip_5;
    }

    strm->next_in = zbuf + 10;
    strm->avail_in = len - 10;

    if (zbuf[3] & 0x8) {
        do {

            if (strm->avail_in == 0) {
                LOG(1, LOG_LINE_NORMAL, L"header error");
                goto gunzip_5;
            }
            --strm->avail_in;
        } while (*strm->next_in++);
    }

    strm->next_out = out_buf;
    strm->avail_out = out_len;
    /*
     * The wrapper consumed the gzip header itself. Negative window bits
     * select raw DEFLATE, so inflate starts at the first compressed block and
     * stops before the gzip trailer.
     */
    rc = zlib_inflateInit2(strm, -MAX_WBITS);

#ifdef CONFIG_ZLIB_DFLTCC

#else
    if (!flush) {
        WS(strm)->inflate_state.wsize = 0;
        WS(strm)->inflate_state.window = NULL;
    }
#endif

    while (rc == Z_OK) {
        if (strm->avail_in == 0) {

            len = fill(zbuf, GZIP_IOBUF_SIZE);
            if (len < 0) {
                rc = -1;
                LOG(1, LOG_LINE_NORMAL, L"read error");
                break;
            }
            strm->next_in = zbuf;
            strm->avail_in = len;
        }
        rc = zlib_inflate(strm, 0);

        if (flush && strm->next_out > out_buf) {
            long l = strm->next_out - out_buf;
            if (l != flush(out_buf, l)) {
                rc = -1;
                LOG(1, LOG_LINE_NORMAL, L"write error");
                break;
            }
            strm->next_out = out_buf;
            strm->avail_out = out_len;
        }

        if (rc == Z_STREAM_END) {
            /* total_out counts decoded bytes across all flushes. Only a
             * complete stream may supply a length to the image loader. */
            if (decoded_size)
                *decoded_size = strm->total_out;
            rc = 0;
            break;
        } else if (rc != Z_OK) {
            LOG(1, LOG_LINE_NORMAL, L"uncompression error");
            rc = -1;
        }
    }

    zlib_inflateEnd(strm);

gunzip_5:
    free(strm->workspace);
gunzip_nomem4:
    free(strm);
gunzip_nomem3:
    if (!buf)
        free(zbuf);
gunzip_nomem2:
    if (flush)
        free(out_buf);
gunzip_nomem1:
    return rc;  
}
