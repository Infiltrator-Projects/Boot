/* SPDX-License-Identifier: GPL-3.0-or-later
 * Exercise the production FSW equality/coercion entry points against counted
 * slices with exact-size stack objects. Sanitizers catch any access beyond
 * those objects, including the previously unbounded continuation reads.
 */
#include "fsw_core.h"
#include <assert.h>

static void rejects(struct fsw_string *bad, struct fsw_string *reference)
{
    struct fsw_string out = FSW_STRING_INIT;
    assert(!fsw_streq(bad, reference));
    assert(!fsw_streq(reference, bad));
    assert(!fsw_streq(bad, bad));
    assert(fsw_strdup_coerce(&out, FSW_STRING_TYPE_UTF16, bad) ==
           FSW_VOLUME_CORRUPTED);
    assert(fsw_strdup_coerce(&out, bad->type, bad) == FSW_VOLUME_CORRUPTED);
    assert(out.data == NULL);
}

int main(void)
{
    fsw_u8 latin[] = {0xe9};
    fsw_u8 utf8[] = {0xc3, 0xa9};
    fsw_u16 utf16[] = {0xe9};
    fsw_u8 truncated[] = {0xc3};
    fsw_u8 bad_tail[] = {0xc3, 0x41};
    fsw_u8 bad_lead[] = {0x80};
    fsw_u8 four_truncated[] = {0xf0, 0x90, 0x80};
    struct fsw_string a = {FSW_STRING_TYPE_ISO88591, 1, 1, latin};
    struct fsw_string b = {FSW_STRING_TYPE_UTF8, 1, 2, utf8};
    struct fsw_string c = {FSW_STRING_TYPE_UTF16, 1, 2, utf16};
    struct fsw_string empty = FSW_STRING_INIT, out = FSW_STRING_INIT;
    struct fsw_string cases[] = {
        {FSW_STRING_TYPE_UTF8, 1, 1, truncated},
        {FSW_STRING_TYPE_UTF8, 1, 2, bad_tail},
        {FSW_STRING_TYPE_UTF8, 1, 1, bad_lead},
        {FSW_STRING_TYPE_UTF8, 1, 3, four_truncated},
        {FSW_STRING_TYPE_UTF8, 2, 2, utf8},
        {FSW_STRING_TYPE_UTF8, 0, 2, utf8},
        {FSW_STRING_TYPE_UTF8, 1, 2, NULL},
        {FSW_STRING_TYPE_UTF8, -1, 2, utf8},
        {FSW_STRING_TYPE_ISO88591, 2, 1, latin},
        {FSW_STRING_TYPE_UTF16, 1, 1, utf16},
        {FSW_STRING_TYPE_UTF16, 2, 2, utf16},
        {FSW_STRING_TYPE_UTF16_SWAPPED, 1, 1, utf16},
        {FSW_STRING_TYPE_ISO88591, 1, -1, latin}
    };
    assert(fsw_streq(&a, &b));
    assert(fsw_streq(&b, &c));
    assert(fsw_streq(&c, &a));
    assert(fsw_streq(&empty, &empty));
    assert(fsw_strdup_coerce(&out, FSW_STRING_TYPE_UTF16, &b) == FSW_SUCCESS);
    assert(fsw_streq(&out, &c));
    fsw_strfree(&out);
    assert(fsw_strdup_coerce(&out, FSW_STRING_TYPE_UTF8, &c) == FSW_SUCCESS);
    assert(fsw_streq(&out, &b));
    fsw_strfree(&out);
    assert(fsw_strdup_coerce(&out, FSW_STRING_TYPE_UTF16, &empty) == FSW_SUCCESS);
    assert(out.len == 0 && out.size == 0 && out.data == NULL);
    for (unsigned i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i)
        rejects(&cases[i], &c);
    puts("FSW counted string bounds regressions passed");
    return 0;
}
