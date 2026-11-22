/*
 * Allocation and string conversion helpers for the FSW library. Strings
 * carry explicit encoding, length and allocation ownership; a byte length is
 * not automatically a character count. Caller-provided destination capacity
 * and invalid-encoding behaviour are critical when reading filesystem names.
 */

#include "fsw_core.h"

#include "fsw_strfunc.h"

/*
 * Allocates and zero-initialises an FSW-owned buffer. The result must not
 * be accessed unless status reports allocation success.
 */
fsw_status_t fsw_alloc_zero(int len, void **ptr_out)
{
    fsw_status_t status;

    status = fsw_alloc(len, ptr_out);
    if (status)
        return status;
    fsw_memzero(*ptr_out, len);
    return FSW_SUCCESS;
}

/*
 * Duplicate exactly len bytes into a newly allocated FSW buffer. Both src
 * validity and nonnegative len are caller obligations; no independent
 * pointer/length validation is performed before the memory copy.
 */
fsw_status_t fsw_memdup(void **dest_out, void *src, int len)
{
    fsw_status_t status;

    status = fsw_alloc(len, dest_out);
    if (status)
        return status;
    fsw_memcpy(*dest_out, src, len);
    return FSW_SUCCESS;
}

int fsw_strlen(struct fsw_string *s)
{
    if (s->type == FSW_STRING_TYPE_EMPTY)
        return 0;
    return s->len;
}

/**
 * First compare logical lengths. Equal encodings also require equal byte lengths
 * and then use a byte comparison. Mixed encodings dispatch to a generated
 * pairwise comparator. Empty values compare equal across encodings, independently
 * of their data pointers.
 */
int fsw_streq(struct fsw_string *s1, struct fsw_string *s2)
{
    struct fsw_string temp_s;

    if (s1->type == FSW_STRING_TYPE_EMPTY) {
        temp_s.type = FSW_STRING_TYPE_ISO88591;
        temp_s.size = temp_s.len = 0;
        temp_s.data = NULL;
        return fsw_streq(&temp_s, s2);
    }
    if (s2->type == FSW_STRING_TYPE_EMPTY) {
        temp_s.type = FSW_STRING_TYPE_ISO88591;
        temp_s.size = temp_s.len = 0;
        temp_s.data = NULL;
        return fsw_streq(s1, &temp_s);
    }

    if (s1->len != s2->len)
        return 0;
    if (s1->len == 0)    
        return 1;

    if (s1->type == s2->type) {

        if (s1->size != s2->size)
            return 0;
        return fsw_memeq(s1->data, s2->data, s1->size);
    }

    #define STREQ_DISPATCH(type1, type2) \
      if (s1->type == FSW_STRING_TYPE_##type1 && s2->type == FSW_STRING_TYPE_##type2) \
        return fsw_streq_##type1##_##type2(s1->data, s2->data, s1->len); \
      if (s2->type == FSW_STRING_TYPE_##type1 && s1->type == FSW_STRING_TYPE_##type2) \
        return fsw_streq_##type1##_##type2(s2->data, s1->data, s1->len);
    STREQ_DISPATCH(ISO88591, UTF8);
    STREQ_DISPATCH(ISO88591, UTF16);
    STREQ_DISPATCH(ISO88591, UTF16_SWAPPED);
    STREQ_DISPATCH(UTF8, UTF16);
    STREQ_DISPATCH(UTF8, UTF16_SWAPPED);
    STREQ_DISPATCH(UTF16, UTF16_SWAPPED);

    return 0;
}

/*
 * Compare a counted filesystem string against a borrowed NUL-terminated
 * ISO-8859-1 byte sequence. The scan of s2 has no supplied maximum, so its
 * caller must guarantee that s2 points to a valid terminated buffer rather
 * than raw unbounded directory-entry bytes.
 */
int fsw_streq_cstr(struct fsw_string *s1, const char *s2)
{
    struct fsw_string temp_s;
    int i;

    for (i = 0; s2[i]; i++)
        ;

    temp_s.type = FSW_STRING_TYPE_ISO88591;
    temp_s.size = temp_s.len = i;
    temp_s.data = (char *)s2;

    return fsw_streq(s1, &temp_s);
}

/**
 * Construct independently owned, counted storage in the requested encoding.
 * Identical encodings copy exactly src->size bytes and append no terminator. An
 * empty input produces a typed zero-length result without an allocation.
 * Unsupported conversion pairs return FSW_UNSUPPORTED.
 */
fsw_status_t fsw_strdup_coerce(struct fsw_string *dest, int type, struct fsw_string *src)
{
    fsw_status_t    status;

    if (src->type == FSW_STRING_TYPE_EMPTY || src->len == 0) {
        dest->type = type;
        dest->size = dest->len = 0;
        dest->data = NULL;
        return FSW_SUCCESS;
    }

    if (src->type == type) {
        dest->type = type;
        dest->len  = src->len;
        dest->size = src->size;
        status = fsw_alloc(dest->size, &dest->data);
        if (status)
            return status;

        fsw_memcpy(dest->data, src->data, dest->size);
        return FSW_SUCCESS;
    }

    #define STRCOERCE_DISPATCH(type1, type2) \
      if (src->type == FSW_STRING_TYPE_##type1 && type == FSW_STRING_TYPE_##type2) \
        return fsw_strcoerce_##type1##_##type2(src->data, src->len, dest);
    STRCOERCE_DISPATCH(UTF8, ISO88591);
    STRCOERCE_DISPATCH(UTF16, ISO88591);
    STRCOERCE_DISPATCH(UTF16_SWAPPED, ISO88591);
    STRCOERCE_DISPATCH(ISO88591, UTF8);
    STRCOERCE_DISPATCH(UTF16, UTF8);
    STRCOERCE_DISPATCH(UTF16_SWAPPED, UTF8);
    STRCOERCE_DISPATCH(ISO88591, UTF16);
    STRCOERCE_DISPATCH(UTF8, UTF16);
    STRCOERCE_DISPATCH(UTF16_SWAPPED, UTF16);

    return FSW_UNSUPPORTED;
}

/**
 * Return a view into the caller's existing name buffer. For native UTF-16, len
 * advances in code units while size advances in bytes. The unconsumed view skips
 * the separator. Neither view owns a fresh allocation, so its storage must
 * outlive path traversal.
 */
void fsw_strsplit(struct fsw_string *element, struct fsw_string *buffer, char separator)
{
    int i, maxlen;

    if (buffer->type == FSW_STRING_TYPE_EMPTY || buffer->len == 0) {
        element->type = FSW_STRING_TYPE_EMPTY;
        return;
    }

    maxlen = buffer->len;
    /*
     * Both descriptors now view the same storage. Splitting advances the
     * remaining view past the separator and shortens the element view. Only
     * the owner of the original allocation may release it.
     */
    *element = *buffer;

    if (buffer->type == FSW_STRING_TYPE_ISO88591) {
        fsw_u8 *p;

        p = (fsw_u8 *)element->data;
        for (i = 0; i < maxlen; i++, p++) {
            if (*p == separator) {
                buffer->data = p + 1;
                buffer->len -= i + 1;
                break;
            }
        }
        element->len = i;
        if (i == maxlen) {
            buffer->data = p;
            buffer->len -= i;
        }

        element->size = element->len;
        buffer->size  = buffer->len;

    } else if (buffer->type == FSW_STRING_TYPE_UTF16) {
        fsw_u16 *p;

        p = (fsw_u16 *)element->data;
        for (i = 0; i < maxlen; i++, p++) {
            if (*p == separator) {
                buffer->data = p + 1;
                buffer->len -= i + 1;
                break;
            }
        }
        element->len = i;
        if (i == maxlen) {
            buffer->data = p;
            buffer->len -= i;
        }

        element->size = element->len * sizeof(fsw_u16);
        buffer->size  = buffer->len  * sizeof(fsw_u16);

    } else {

        buffer->type = FSW_STRING_TYPE_EMPTY;
    }

}

/*
 * Releases the allocation associated with an FSW string; callers must not
 * retain the data pointer after this function returns.
 */
void fsw_strfree(struct fsw_string *s)
{
    if (s->type != FSW_STRING_TYPE_EMPTY && s->data)
        fsw_free(s->data);
    s->type = FSW_STRING_TYPE_EMPTY;
}
