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
#include "../include/refit_call_wrapper.h"

/*
 * Search by clearing bit 5 of CHAR16 code units. This reproduces
 * ASCII case folding for letters but also equates some punctuation
 * and is not Unicode collation. A NULL input never matches.
 */
BOOLEAN StriSubCmp(IN CHAR16 *SmallStr, IN CHAR16 *BigStr) {
    BOOLEAN Found = 0, Terminate = 0;
    UINTN BigIndex = 0, SmallIndex = 0, BigStart = 0;

    if (SmallStr && BigStr) {
        while (!Terminate) {
            if (BigStr[BigIndex] == '\0') {
                Terminate = 1;
            }
            if (SmallStr[SmallIndex] == '\0') {
                Found = 1;
                Terminate = 1;
            }
            if ((SmallStr[SmallIndex] & ~0x20) == (BigStr[BigIndex] & ~0x20)) {
                SmallIndex++;
                BigIndex++;
            } else {
                SmallIndex = 0;
                BigStart++;
                BigIndex = BigStart;
            }
        }  
    }  
    return Found;
}  

/*
 * Compares CHAR16 strings using an ASCII-like case fold. Do not interpret
 * this as a locale-aware Unicode filename identity comparison.
 */
BOOLEAN MyStriCmp(IN CONST CHAR16 *FirstString, IN CONST CHAR16 *SecondString) {
    if (FirstString && SecondString) {
        /*
         * Clearing bit 5 implements the ASCII A/a distinction for letters,
         * but also collapses punctuation pairs such as '[' and '{'. Callers
         * using this equality for paths inherit that comparison rule.
         */
        while ((*FirstString != L'\0') && ((*FirstString & ~0x20) == (*SecondString & ~0x20))) {
                FirstString++;
                SecondString++;
        }
        return (*FirstString == *SecondString);
    } else {
        return FALSE;
    }
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

/*
 * Change ASCII uppercase code units in place; non-ASCII text is
 * left unchanged. This is suitable for BOOT's historical ASCII
 * tokens, not locale-sensitive filesystem-name normalisation.
 */
VOID ToLower(CHAR16 * MyString) {
    UINTN i = 0;

    if (MyString) {
        while (MyString[i] != L'\0') {
            if ((MyString[i] >= L'A') && (MyString[i] <= L'Z'))
                MyString[i] = MyString[i] - L'A' + L'a';
            i++;
        }  
    }  
}  

/*
 * Reallocates and replaces the destination string; callers must treat the
 * previous destination allocation as consumed by this operation.
 */
VOID MergeStrings(IN OUT CHAR16 **First, IN CHAR16 *Second, CHAR16 AddChar) {
    UINTN Length1 = 0, Length2 = 0;
    CHAR16* NewString;

    if (*First != NULL)
        Length1 = StrLen(*First);
    if (Second != NULL)
        Length2 = StrLen(Second);
    NewString = AllocatePool(sizeof(CHAR16) * (Length1 + Length2 + 2));
    if (NewString != NULL) {
        if ((*First != NULL) && (Length1 == 0)) {
            MyFreePool(*First);
            *First = NULL;
        }
        NewString[0] = L'\0';
        if (*First != NULL) {
            StrCat(NewString, *First);
            if (AddChar) {
                NewString[Length1] = AddChar;
                NewString[Length1 + 1] = '\0';
            }  
        }  
        if (Second != NULL)
            StrCat(NewString, Second);
        MyFreePool(*First);
        *First = NewString;
    } else {
        Print(L"Error! Unable to allocate memory in MergeStrings()!\n");
    }  
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

/* Canonicalise adjacent spaces in the caller's writable UTF-16 string, then
 * cap its code-unit count. Collapsing a run uses a temporary duplicate, so
 * allocation failure can stop normalisation before truncation occurs.
 * Limit is a count of CHAR16 units, not Unicode scalar values or pixels.
 */
BOOLEAN LimitStringLength(CHAR16 *TheString, UINTN Limit) {
    CHAR16    *SubString, *TempString;
    UINTN     i;
    BOOLEAN   HasChanged = FALSE;

    SubString = MyStrStr(TheString, L"  ");
    while (SubString != NULL) {
        i = 0;
        while (SubString[i] == L' ')
            i++;
        if (i >= StrLen(SubString)) {
            SubString[0] = '\0';
            HasChanged = TRUE;
        } else {
            TempString = StrDuplicate(&SubString[i]);
            if (TempString != NULL) {
                StrCpy(&SubString[1], TempString);
                MyFreePool(TempString);
                HasChanged = TRUE;
            } else {

                break;
            }  
        }  
        SubString = MyStrStr(TheString, L"  ");
    }  

    if (StrLen(TheString) > Limit) {
        TheString[Limit] = '\0';
        HasChanged = TRUE;
    }  

    return HasChanged;
}  

/**
 * Return the inclusive span covering every digit and the first configured extra
 * version string found in the name. Punctuation between the outermost matches
 * survives, so vmlinuz-6.12.3 yields 6.12.3. The end-index test also means a
 * single digit at index zero by itself produces NULL.
 */
CHAR16 *FindNumbers(IN CHAR16 *InString) {
    UINTN i = 0, StartOfElement, EndOfElement = 0, CopyLength;
    CHAR16 *Found = NULL, *ExtraFound = NULL, *LookFor;

    if (InString == NULL)
        return NULL;

    StartOfElement = StrLen(InString);

    while ((ExtraFound == NULL) && (LookFor = FindCommaDelimited(GlobalConfig.ExtraKernelVersionStrings, i++))) {
        if ((ExtraFound = MyStrStr(InString, LookFor))) {
            StartOfElement = ExtraFound - InString;
            EndOfElement = StartOfElement + StrLen(LookFor) - 1;
        }  
        MyFreePool(LookFor);
    }  

    /*
     * Use the span from the earliest to the latest digit, widened by a
     * configured extra version string if one matched. Separators and flavour
     * text inside that span survive, which affects how FindInitrd compares
     * kernel and initrd names.
     */
    for (i = 0; InString[i] != L'\0'; i++) {
        if ((InString[i] >= L'0') && (InString[i] <= L'9')) {
            if (StartOfElement > i)
                StartOfElement = i;
            if (EndOfElement < i)
                EndOfElement = i;
        }  
    }  

    if (EndOfElement > 0) {
        if (EndOfElement >= StartOfElement) {
            CopyLength = EndOfElement - StartOfElement + 1;
            Found = StrDuplicate(&InString[StartOfElement]);
            if (Found != NULL)
                Found[CopyLength] = 0;
        }  
    }  
    return (Found);
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

/**
 * Return an owned copy of the zero-based field. Adjacent or trailing commas can
 * produce a valid empty string. Running out of separators before reaching Index
 * produces NULL. A caller iterating successive indices rescans the prefix on
 * every call.
 */
CHAR16 *FindCommaDelimited(IN CHAR16 *InString, IN UINTN Index) {
    UINTN    StartPos = 0, CurPos = 0, InLength;
    BOOLEAN  Found = FALSE;
    CHAR16   *FoundString = NULL;

    if (InString != NULL) {
        InLength = StrLen(InString);

        while ((Index > 0) && (CurPos < InLength)) {
            if (InString[CurPos] == L',') {
                Index--;
                StartPos = CurPos + 1;
            }  
            CurPos++;
        }  

        while ((CurPos < InLength) && (!Found)) {
            if (InString[CurPos] == L',')
                Found = TRUE;
            else
                CurPos++;
        }  
        if (Index == 0)
            FoundString = StrDuplicate(&InString[StartPos]);
        if (FoundString != NULL)
            FoundString[CurPos - StartPos] = 0;
    }  
    return (FoundString);
}  

/*
 * Delete the first textual substring matching ToDelete, not
 * necessarily a complete comma-delimited field. Callers must avoid
 * ambiguous item names or risk removing part of a larger token;
 * the supplied list is modified in place.
 */
BOOLEAN DeleteItemFromCsvList(CHAR16 *ToDelete, CHAR16 *List) {
    CHAR16 *Found, *Comma;

    if ((ToDelete == NULL) || (List == NULL))
        return FALSE;

    if ((Found = MyStrStr(List, ToDelete)) != NULL) {
        if ((Comma = MyStrStr(Found, L",")) == NULL) {

            if (Found == List) {  
                List[0] = L'\0';
            } else {  
                Found--;
                Found[0] = L'\0';
            }  
        } else {  
            StrCpy(Found, &Comma[1]);
        }  
        return TRUE;
    } else {
        return FALSE;
    }  
}  

/* Exact-element membership in a comma-separated UTF-16 list. Each element
 * is temporarily duplicated and released; comparisons inherit MyStriCmp's
 * case-folding semantics, not a Unicode locale-independent identity rule.
 */
BOOLEAN IsIn(IN CHAR16 *SmallString, IN CHAR16 *List) {
    UINTN     i = 0;
    BOOLEAN   Found = FALSE;
    CHAR16    *OneElement;

    if (SmallString && List) {
        while (!Found && (OneElement = FindCommaDelimited(List, i++))) {
            if (MyStriCmp(OneElement, SmallString))
                Found = TRUE;
            MyFreePool(OneElement);
        }  
    }  
    return Found;
}  

/* Return TRUE if any nonempty CSV element matches BigString's leading
 * substring according to StriSubCmp. This is prefix matching, not arbitrary
 * substring containment; owned temporary list elements are always freed.
 */
BOOLEAN IsInSubstring(IN CHAR16 *BigString, IN CHAR16 *List) {
    UINTN   i = 0, ElementLength;
    BOOLEAN Found = FALSE;
    CHAR16  *OneElement;

    if (BigString && List) {
        while (!Found && (OneElement = FindCommaDelimited(List, i++))) {
            ElementLength = StrLen(OneElement);
            if ((ElementLength <= StrLen(BigString)) &&
                 (ElementLength > 0) &&
                 (StriSubCmp(OneElement, BigString))) {
                    Found = TRUE;
            }  
            MyFreePool(OneElement);
        }  
    }  
    return Found;
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

/* Check ASCII hexadecimal digits through the first UTF-16 NUL. The empty
 * string returns TRUE and NULL is not accepted; callers validating a GUID
 * must additionally establish its exact length and separator positions.
 */
BOOLEAN IsValidHex(CHAR16 *Input) {
    BOOLEAN IsHex = TRUE;
    UINTN i = 0;

    while ((Input[i] != L'\0') && IsHex) {
        if (!(((Input[i] >= L'0') && (Input[i] <= L'9')) ||
              ((Input[i] >= L'A') && (Input[i] <= L'F')) ||
              ((Input[i] >= L'a') && (Input[i] <= L'f')))) {
                IsHex = FALSE;
        }
        i++;
    }  
    return IsHex;
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

    Status = refit_call2_wrapper(ST->RuntimeServices->GetTime, &CurrentTime, NULL);
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
