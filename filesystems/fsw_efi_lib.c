/*
 * String and timestamp conversion helpers for EFI filesystem protocol
 * metadata. EFI_TIME and CHAR16 fields have fixed public layout and cannot
 * be populated by treating POSIX time or on-disk string encodings as native.
 */

#include "fsw_efi.h"

#define SECSPERMIN      60
#define MINSPERHOUR     60
#define HOURSPERDAY     24
#define DAYSPERWEEK     7
#define DAYSPERNYEAR    365
#define DAYSPERLYEAR    366
#define SECSPERHOUR     (SECSPERMIN * MINSPERHOUR)
#define SECSPERDAY      ((long) SECSPERHOUR * HOURSPERDAY)
#define MONSPERYEAR     12

#define EPOCH_YEAR      1970
#define EPOCH_WDAY      TM_THURSDAY

#define isleap(y) (((y) % 4) == 0 && (((y) % 100) != 0 || ((y) % 400) == 0))
#define LEAPS_THRU_END_OF(y)    ((y) / 4 - (y) / 100 + (y) / 400)

static const int mon_lengths[2][MONSPERYEAR] = {
    { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
    { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};
static const int year_lengths[2] = {
    DAYSPERNYEAR, DAYSPERLYEAR
};

/*
 * Maps integer time data to EFI_TIME fields. Calendar range and epoch
 * assumptions must not be confused with the source filesystem's timestamps.
 */
VOID fsw_efi_decode_time(OUT EFI_TIME *EfiTime, IN UINT32 UnixTime)
{
    long        days, rem;
    int         y, newy, yleap;
    const int   *ip;

    ZeroMem(EfiTime, sizeof(EFI_TIME));

    days = UnixTime / SECSPERDAY;
    rem = UnixTime % SECSPERDAY;

    EfiTime->Hour = (UINT8) (rem / SECSPERHOUR);
    rem = rem % SECSPERHOUR;
    EfiTime->Minute = (UINT8) (rem / SECSPERMIN);
    EfiTime->Second = (UINT8) (rem % SECSPERMIN);

    y = EPOCH_YEAR;
    while (days < 0 || days >= (long) year_lengths[yleap = isleap(y)]) {
        newy = y + days / DAYSPERNYEAR;
        if (days < 0)
            --newy;
        days -= (newy - y) * DAYSPERNYEAR +
            LEAPS_THRU_END_OF(newy - 1) -
            LEAPS_THRU_END_OF(y - 1);
        y = newy;
    }
    EfiTime->Year = (UINT16)y;
    ip = mon_lengths[yleap];
    for (EfiTime->Month = 0; days >= (long) ip[EfiTime->Month]; ++(EfiTime->Month))
        days = days - (long) ip[EfiTime->Month];
    EfiTime->Month++;   
    EfiTime->Day = (UINT8) (days + 1);
}

/*
 * Return the byte capacity required for a NUL-terminated UTF-16 EFI
 * name. FSW string length is counted in code units, whereas UEFI
 * buffer negotiation and allocations use bytes.
 */
UINTN fsw_efi_strsize(struct fsw_string *s)
{
    if (s->type == FSW_STRING_TYPE_EMPTY)
        return sizeof(CHAR16);
    return (s->len + 1) * sizeof(CHAR16);
}

/*
 * The EFI host requests UTF-16 when constructing the FSW volume. CopyMem
 * below therefore copies already-converted bytes. An unexpected encoding
 * produces an empty string.
 */
VOID fsw_efi_strcpy(CHAR16 *Dest, struct fsw_string *src)
{
    if ((src->type == FSW_STRING_TYPE_EMPTY) | (src->size == 0)) {
        Dest[0] = 0;
    } else if (src->type == FSW_STRING_TYPE_UTF16) {
        refit_call3_wrapper(gBS->CopyMem, Dest, src->data, src->size);
        Dest[src->len] = 0;
    } else {

        Dest[0] = 0;
    }
}
