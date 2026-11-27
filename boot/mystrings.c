/*
 * BOOT / Strings and identifiers
 *
 * UTF-16, GUID and delimited-list helpers. Callers must distinguish
 * borrowed strings from allocated results. Case folding is ASCII-oriented
 * rather than Unicode collation.
 */

#include "mystrings.h"
#include "lib.h"
#include "screen.h"
#include "../include/boot_call_wrapper.h"

/* Fold only ASCII letters. Clearing bit 5 unconditionally conflates
 * punctuation and even maps a space to NUL, which can overrun a string. */
static CHAR16 AsciiLower(CHAR16 c) {
    return c >= L'A' && c <= L'Z' ? c - L'A' + L'a' : c;
}

/* Compare a bounded token against a NUL-terminated string. Stop at the
 * terminator before accessing another code unit, even for malformed input. */
static BOOLEAN FoldedPrefix(CONST CHAR16 *Text, CONST CHAR16 *Token, UINTN Length) {
    while (Length--) {
        if (!*Text || AsciiLower(*Text++) != AsciiLower(*Token++))
            return FALSE;
    }
    return TRUE;
}

static BOOLEAN FoldedContains(CONST CHAR16 *Text, CONST CHAR16 *Token, UINTN Length) {
    if (!Length)
        return TRUE;
    for (; *Text; ++Text) {
        if (FoldedPrefix(Text, Token, Length))
            return TRUE;
    }
    return FALSE;
}

/* Case-insensitive substring containment; NULL never matches. Only ASCII
 * letters fold, so distinct punctuation and non-ASCII code units stay distinct. */
BOOLEAN StriSubCmp(IN CHAR16 *SmallStr, IN CHAR16 *BigStr) {
    return SmallStr && BigStr && FoldedContains(BigStr, SmallStr, StrLen(SmallStr));
}

/* Compare complete UTF-16 strings using the same ASCII fold as containment. */
BOOLEAN MyStriCmp(IN CONST CHAR16 *FirstString, IN CONST CHAR16 *SecondString) {
    if (!FirstString || !SecondString)
        return FALSE;
    while (*FirstString && AsciiLower(*FirstString) == AsciiLower(*SecondString)) {
        ++FirstString;
        ++SecondString;
    }
    return *FirstString == *SecondString;
}

/*
 * Return a borrowed pointer to the first exact UTF-16 substring
 * match. The returned location aliases String and becomes invalid
 * when the original allocation is freed or modified.
 */
CHAR16* MyStrStr (IN CHAR16  *String, IN CHAR16  *StrCharSet)
{
    CHAR16 *Src;
    CHAR16 *Sub;

    if ((String == NULL) || (StrCharSet == NULL))
        return NULL;

    Src = String;
    Sub = StrCharSet;

    while ((*String != L'\0') && (*StrCharSet != L'\0')) {
        if (*String++ != *StrCharSet) {
            String = ++Src;
            StrCharSet = Sub;
        } else {
            StrCharSet++;
        }
    }
    if (*StrCharSet == L'\0') {
        return Src;
    } else {
        return NULL;
    }
}  

/* Apply the common ASCII fold in place without altering non-ASCII text. */
VOID ToLower(CHAR16 *MyString) {
    if (MyString) {
        for (; *MyString; ++MyString)
            *MyString = AsciiLower(*MyString);
    }
}

/* Allocate exactly one replacement, preserving the original until all
 * borrowed input has been copied. Second may alias the original allocation.
 * A separator is appended only when the original string is nonempty. */
VOID MergeStrings(IN OUT CHAR16 **First, IN CHAR16 *Second, CHAR16 AddChar) {
    UINTN Length1, Length2, Separator, MaxChars, At;
    CHAR16 *NewString;

    if (!First)
        return;
    Length1 = *First ? StrLen(*First) : 0;
    Length2 = Second ? StrLen(Second) : 0;
    Separator = Length1 && AddChar ? 1 : 0;
    MaxChars = ((UINTN)-1) / sizeof(CHAR16);
    if (Length1 > MaxChars - 1 - Separator || Length2 > MaxChars - 1 - Separator - Length1)
        return;
    NewString = AllocatePool((Length1 + Length2 + Separator + 1) * sizeof(CHAR16));
    if (!NewString) {
        Print(L"Error! Unable to allocate memory in MergeStrings()!\n");
        return;
    }
    if (Length1)
        CopyMem(NewString, *First, Length1 * sizeof(CHAR16));
    At = Length1;
    if (Separator)
        NewString[At++] = AddChar;
    if (Length2)
        CopyMem(NewString + At, Second, Length2 * sizeof(CHAR16));
    NewString[At + Length2] = L'\0';
    MyFreePool(*First);
    *First = NewString;
}

/*
 * Split a temporary duplicate on spaces, colons, underscores and
 * hyphens, then append each nonempty token to the destination list.
 * SourceString is borrowed; MergeStrings owns any destination
 * replacement allocation.
 */
VOID MergeWords(CHAR16 **MergeTo, CHAR16 *SourceString, CHAR16 AddChar) {
    CHAR16 *Temp, *Word, *p;
    BOOLEAN LineFinished = FALSE;

    if (SourceString) {
        Temp = Word = p = StrDuplicate(SourceString);
        if (Temp) {
            while (!LineFinished) {
                if ((*p == L' ') || (*p == L':') || (*p == L'_') || (*p == L'-') || (*p == L'\0')) {
                    if (*p == L'\0')
                        LineFinished = TRUE;
                    *p = L'\0';
                    if (*Word != L'\0')
                        MergeStrings(MergeTo, Word, AddChar);
                    Word = p + 1;
                }  
                p++;
            }  
            MyFreePool(Temp);
        } else {
            Print(L"Error! Unable to allocate memory in MergeWords()!\n");
        }  
    }  
}  

/* Collapse repeated spaces and truncate in one forward, in-place pass.
 * As before, a trailing run of two or more spaces is discarded; a single
 * trailing space is retained. The write index never exceeds the read index.
 * Limit counts CHAR16 units rather than Unicode scalars or rendered pixels. */
BOOLEAN LimitStringLength(CHAR16 *TheString, UINTN Limit) {
    UINTN Read = 0, Write = 0;
    BOOLEAN Changed = FALSE;

    if (!TheString)
        return FALSE;
    while (TheString[Read]) {
        CHAR16 c = TheString[Read++];
        if (c == L' ' && TheString[Read] == L' ') {
            do { ++Read; } while (TheString[Read] == L' ');
            Changed = TRUE;
            if (!TheString[Read])
                break;
        }
        if (Write == Limit) {
            Changed = TRUE;
            break;
        }
        TheString[Write++] = c;
    }
    TheString[Write] = L'\0';
    return Changed;
}

/* Copy an exact UTF-16 span with its own terminator and checked allocation.
 * The caller owns the returned EFI pool allocation. */
static CHAR16 *DuplicateSpan(CONST CHAR16 *Start, UINTN Length) {
    CHAR16 *Result;
    if (Length >= ((UINTN)-1) / sizeof(CHAR16))
        return NULL;
    Result = AllocatePool((Length + 1) * sizeof(CHAR16));
    if (Result) {
        if (Length)
            CopyMem(Result, Start, Length * sizeof(CHAR16));
        Result[Length] = L'\0';
    }
    return Result;
}

/* Return the span covering every digit and the first nonempty configured
 * extra version token found in the name. End is exclusive, so a single digit
 * at index zero is a valid version. Empty configured fields never match. */
CHAR16 *FindNumbers(IN CHAR16 *InString) {
    UINTN i, Start, End = 0;
    CHAR16 *LookFor, *ExtraFound = NULL;

    if (!InString)
        return NULL;
    Start = StrLen(InString);
    for (i = 0; !ExtraFound && (LookFor = FindCommaDelimited(GlobalConfig.ExtraKernelVersionStrings, i)); ++i) {
        if (*LookFor && (ExtraFound = MyStrStr(InString, LookFor))) {
            Start = (UINTN)(ExtraFound - InString);
            End = Start + StrLen(LookFor);
        }
        MyFreePool(LookFor);
    }
    for (i = 0; InString[i]; ++i) {
        if (InString[i] >= L'0' && InString[i] <= L'9') {
            if (Start > i)
                Start = i;
            if (End <= i)
                End = i + 1;
        }
    }
    return End > Start ? DuplicateSpan(InString + Start, End - Start) : NULL;
}

/* Count equal leading UTF-16 code units, stopping at either NUL terminator.
 * This is a binary-prefix length, not case folding or Unicode normalisation;
 * NULL inputs intentionally produce an empty common prefix.
 */
UINTN NumCharsInCommon(IN CHAR16* String1, IN CHAR16* String2) {
    UINTN Count = 0;
    if ((String1 == NULL) || (String2 == NULL))
        return 0;
    while ((String1[Count] != L'\0') && (String2[Count] != L'\0') && (String1[Count] == String2[Count]))
        Count++;
    return Count;
}  

/* Locate a CSV field's exclusive end without allocating or changing its
 * input. A terminating NUL represents the last (possibly empty) field. */
static CHAR16 *CsvFieldEnd(CHAR16 *Field) {
    while (*Field && *Field != L',')
        ++Field;
    return Field;
}

/* Return an owned copy of exactly one zero-based field. Empty fields are
 * valid; an index beyond the final field returns NULL. */
CHAR16 *FindCommaDelimited(IN CHAR16 *InString, IN UINTN Index) {
    CHAR16 *End;
    if (!InString)
        return NULL;
    while (Index--) {
        End = CsvFieldEnd(InString);
        if (!*End)
            return NULL;
        InString = End + 1;
    }
    End = CsvFieldEnd(InString);
    return DuplicateSpan(InString, (UINTN)(End - InString));
}

/* Remove the first exact, case-sensitive CSV field, never a substring of
 * another hidden entry. Copy forwards because the destination precedes the
 * source; deleting the final field also removes its preceding delimiter. */
BOOLEAN DeleteItemFromCsvList(CHAR16 *ToDelete, CHAR16 *List) {
    UINTN Length;
    CHAR16 *Field, *End;
    if (!ToDelete || !*ToDelete || !List)
        return FALSE;
    Length = StrLen(ToDelete);
    for (Field = List; ; Field = End + 1) {
        End = CsvFieldEnd(Field);
        if ((UINTN)(End - Field) == Length && !CompareMem(Field, ToDelete, Length * sizeof(CHAR16))) {
            if (*End) {
                ++End;
                while ((*Field++ = *End++) != L'\0') {}
            } else {
                if (Field != List)
                    --Field;
                *Field = L'\0';
            }
            return TRUE;
        }
        if (!*End)
            return FALSE;
    }
}

/* Exact case-insensitive CSV membership in one pass with no pool allocation. */
BOOLEAN IsIn(IN CHAR16 *SmallString, IN CHAR16 *List) {
    UINTN Length;
    CHAR16 *End;
    if (!SmallString || !List)
        return FALSE;
    Length = StrLen(SmallString);
    for (;; List = End + 1) {
        End = CsvFieldEnd(List);
        if ((UINTN)(End - List) == Length && FoldedPrefix(SmallString, List, Length))
            return TRUE;
        if (!*End)
            return FALSE;
    }
}

/* Match any nonempty CSV field anywhere in BigString, using the same ASCII
 * containment semantics as StriSubCmp without allocating temporary fields. */
BOOLEAN IsInSubstring(IN CHAR16 *BigString, IN CHAR16 *List) {
    CHAR16 *End;
    if (!BigString || !List)
        return FALSE;
    for (;; List = End + 1) {
        End = CsvFieldEnd(List);
        if (End != List && FoldedContains(BigString, List, (UINTN)(End - List)))
            return TRUE;
        if (!*End)
            return FALSE;
    }
}

/**
 * Replace only the first nonempty SearchString occurrence. A percent sign
 * immediately preceding that occurrence escapes the substitution: remove the
 * escape and insert SearchString literally instead of ReplString. A successful
 * call replaces the caller-owned pool allocation; a failed match, invalid
 * argument or allocation failure leaves the caller's string untouched.
 *
 * Allocate exactly once after checking the UTF-16 code-unit arithmetic. The
 * original cannot be modified or released until the complete replacement has
 * been assembled. This matters for kernel command lines and loader options:
 * both can contain user-supplied percent tokens and arbitrary suffixes.
 */
BOOLEAN ReplaceSubstring(IN OUT CHAR16 **MainString, IN CHAR16 *SearchString, IN CHAR16 *ReplString) {
    CHAR16 *Match, *NewString, *Tail, *Replacement;
    UINTN SearchLength, PrefixLength, ReplacementLength, TailLength;
    UINTN MaxChars, ResultLength;

    if (MainString == NULL || *MainString == NULL || SearchString == NULL || ReplString == NULL)
        return FALSE;

    SearchLength = StrLen(SearchString);
    if (SearchLength == 0)
        return FALSE;

    Match = MyStrStr(*MainString, SearchString);
    if (Match == NULL)
        return FALSE;

    PrefixLength = (UINTN)(Match - *MainString);
    Replacement = ReplString;
    if (PrefixLength > 0 && Match[-1] == L'%') {
        --PrefixLength;
        Replacement = SearchString;
    }

    Tail = Match + SearchLength;
    ReplacementLength = StrLen(Replacement);
    TailLength = StrLen(Tail);

    /*
     * ResultLength includes the terminal CHAR16. Reject size_t overflow before
     * multiplying by sizeof(CHAR16), not after an untrusted length wraps.
     */
    MaxChars = ((UINTN)-1) / sizeof(CHAR16);
    if (PrefixLength >= MaxChars ||
        ReplacementLength > MaxChars - 1 - PrefixLength ||
        TailLength > MaxChars - 1 - PrefixLength - ReplacementLength)
        return FALSE;

    ResultLength = PrefixLength + ReplacementLength + TailLength + 1;
    NewString = AllocatePool(ResultLength * sizeof(CHAR16));
    if (NewString == NULL)
        return FALSE;

    CopyMem(NewString, *MainString, PrefixLength * sizeof(CHAR16));
    CopyMem(NewString + PrefixLength, Replacement, ReplacementLength * sizeof(CHAR16));
    CopyMem(NewString + PrefixLength + ReplacementLength, Tail, TailLength * sizeof(CHAR16));
    NewString[ResultLength - 1] = L'\0';

    MyFreePool(*MainString);
    *MainString = NewString;
    return TRUE;
}

/* An empty string is valid; NULL is not. Exact-width users must separately
 * validate the expected digit count and separator positions. */
BOOLEAN IsValidHex(CHAR16 *Input) {
    if (!Input)
        return FALSE;
    for (; *Input; ++Input) {
        CHAR16 c = AsciiLower(*Input);
        if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f')))
            return FALSE;
    }
    return TRUE;
}

/*
 * Accumulate up to sixteen hexadecimal digits into UINT64 from
 * a character offset. Invalid or truncated input can yield a
 * partial value or zero; callers requiring an exact width must
 * validate the complete source separately.
 */
UINT64 StrToHex(CHAR16 *Input, UINTN Pos, UINTN NumChars) {
    UINT64 retval = 0x00;
    UINTN  NumDone = 0, InputLength;
    CHAR16 a;

    if ((Input == NULL) || (NumChars == 0) || (NumChars > 16)) {
        return 0;
    }

    InputLength = StrLen(Input);
    while ((Pos <= InputLength) && (NumDone < NumChars)) {
        a = Input[Pos];
        if ((a >= '0') && (a <= '9')) {
            retval *= 0x10;
            retval += (a - '0');
            NumDone++;
        }
        if ((a >= 'a') && (a <= 'f')) {
            retval *= 0x10;
            retval += (a - 'a' + 0x0a);
            NumDone++;
        }
        if ((a >= 'A') && (a <= 'F')) {
            retval *= 0x10;
            retval += (a - 'A' + 0x0a);
            NumDone++;
        }
        Pos++;
    }  
    return retval;
}  

/*
 * Accept exactly eight-four-four-four-twelve hexadecimal digits separated
 * by hyphens. Every non-separator position must be in [0-9A-Fa-f]; the three
 * disjoint range checks must each use OR for the out-of-range condition.
 * This validates syntax, not the existence of a partition or EFI protocol.
 */
BOOLEAN IsGuid(CHAR16 *UnknownString) {
    UINTN   Length, i;
    BOOLEAN retval = TRUE;
    CHAR16  a;

    if (UnknownString == NULL)
        return FALSE;

    Length = StrLen(UnknownString);
    if (Length != 36)
        return FALSE;

    for (i = 0; i < Length; i++) {
        a = UnknownString[i];
        if ((i == 8) || (i == 13) || (i == 18) || (i == 23)) {
            if (a != L'-')
                retval = FALSE;
        } else if (((a < L'a') || (a > L'f')) &&
                   ((a < L'A') || (a > L'F')) &&
                   ((a < L'0') || (a > L'9'))) {
            retval = FALSE;
        }  
    }  
    return retval;
}  

/*
 * Format GUID fields as an allocated canonical hex string. The
 * return value is caller-owned EFI pool storage and does not
 * borrow any bytes from the source GUID.
 */
CHAR16 * GuidAsString(EFI_GUID *GuidData) {
    CHAR16 *TheString;

    TheString = AllocateZeroPool(42 * sizeof(CHAR16));
    if (GuidData && (TheString != 0)) {
        SPrint (TheString, 82, L"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                (UINTN)GuidData->Data1, (UINTN)GuidData->Data2, (UINTN)GuidData->Data3,
                (UINTN)GuidData->Data4[0], (UINTN)GuidData->Data4[1], (UINTN)GuidData->Data4[2],
                (UINTN)GuidData->Data4[3], (UINTN)GuidData->Data4[4], (UINTN)GuidData->Data4[5],
                (UINTN)GuidData->Data4[6], (UINTN)GuidData->Data4[7]);
    }
    return TheString;
}  

/* Convert the canonical 8-4-4-4-12 hex layout after IsGuid validation.
 * Failure returns the all-zero GUID sentinel; callers must not interpret
 * that sentinel as a valid nonzero identity supplied by firmware.
 */
EFI_GUID StringAsGuid(CHAR16 * InString) {
    EFI_GUID  Guid = NULL_GUID_VALUE;

    if (!IsGuid(InString)) {
        return Guid;
    }

    Guid.Data1 = (UINT32) StrToHex(InString, 0, 8);
    Guid.Data2 = (UINT16) StrToHex(InString, 9, 4);
    Guid.Data3 = (UINT16) StrToHex(InString, 14, 4);
    Guid.Data4[0] = (UINT8) StrToHex(InString, 19, 2);
    Guid.Data4[1] = (UINT8) StrToHex(InString, 21, 2);
    Guid.Data4[2] = (UINT8) StrToHex(InString, 23, 2);
    Guid.Data4[3] = (UINT8) StrToHex(InString, 26, 2);
    Guid.Data4[4] = (UINT8) StrToHex(InString, 28, 2);
    Guid.Data4[5] = (UINT8) StrToHex(InString, 30, 2);
    Guid.Data4[6] = (UINT8) StrToHex(InString, 32, 2);
    Guid.Data4[7] = (UINT8) StrToHex(InString, 34, 2);

    return Guid;
}  

CHAR16 *GetTimeString(VOID) {
    CHAR16  *TimeStr = NULL;
    EFI_TIME CurrentTime;
    EFI_STATUS Status = EFI_SUCCESS;

    Status = boot_call2_wrapper(ST->RuntimeServices->GetTime, &CurrentTime, NULL);
    if (EFI_ERROR(Status)) {
        TimeStr = PoolPrint(L"unknown time");
    } else {
        TimeStr = PoolPrint(L"%02d:%02d:%02d",
                            CurrentTime.Hour,
                            CurrentTime.Minute,
                            CurrentTime.Second);
    }
    return TimeStr;
}  

/*
 * Release every separately allocated list value and link node.
 * No caller pointer is reset; keeping an alias after this
 * operation risks a second free or use-after-free.
 */
VOID DeleteStringList(STRING_LIST *StringList) {
    STRING_LIST *Current = StringList, *Previous;

    while (Current != NULL) {
        MyFreePool(Current->Value);
        Previous = Current;
        Current = Current->Next;
        MyFreePool(Previous);
    }
}  
