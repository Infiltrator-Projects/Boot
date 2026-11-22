#!/usr/bin/env bash
# BOOT / BootOrder regression tests
#
# Runs production firmware-ordering routines against a mocked EFI variable
# store.
#
# Success exercises ordering semantics, not persistence across every real
# firmware implementation.

set -euo pipefail
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
{
cat <<'HEAD'
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
typedef size_t UINTN;
typedef uint16_t UINT16;
typedef char CHAR8;
typedef wchar_t CHAR16;
typedef unsigned long EFI_STATUS;
typedef int BOOLEAN;
typedef void VOID;
typedef struct { int unused; } EFI_GUID;
static EFI_GUID GlobalGuid;
#define EFI_SUCCESS 0UL
#define EFI_NOT_FOUND 1UL
#define EFI_INVALID_PARAMETER 2UL
#define EFI_COMPROMISED_DATA 3UL
#define EFI_OUT_OF_RESOURCES 4UL
#define EFI_DEVICE_ERROR 5UL
#define EFI_ERROR(s) ((s) != EFI_SUCCESS)
#define TRUE 1
static UINT16 entries[16];
static size_t nentries;
static unsigned writes, writes_fail, failed_read, malformed;
static UINT16 missing;
static void MyFreePool(void *p) { free(p); }
static void *AllocateZeroPool(UINTN n) { return calloc(1,n); }
static CHAR16 *PoolPrint(const CHAR16 *fmt, UINTN n) {
    CHAR16 *out=calloc(40,sizeof(*out));
    if(out) swprintf(out,40,fmt,(unsigned)n);
    return out;
}
static EFI_STATUS EfivarGetRaw(EFI_GUID *guid, const CHAR16 *name, CHAR8 **out, UINTN *size) {
    unsigned no=0;
    (void)guid;
    *out=NULL;
    *size=0;
    if (wcscmp(name,L"BootOrder")==0) {
        *size=nentries*sizeof(entries[0]) + (malformed ? 1 : 0);
        *out=malloc(*size ? *size : 1);
        if(!*out) abort();
        memcpy(*out,entries,nentries*sizeof(entries[0]));
        return EFI_SUCCESS;
    }
    if(swscanf(name,L"Boot%4x",&no)!=1) abort();
    if(no==missing) return EFI_NOT_FOUND;
    if(no==failed_read) return EFI_DEVICE_ERROR;
    *size=8;
    *out=calloc(*size,1);
    if(!*out) abort();
    return EFI_SUCCESS;
}
static EFI_STATUS EfivarSetRaw(EFI_GUID *guid, const CHAR16 *name,
                               CHAR8 *buf, UINTN size, BOOLEAN persistent) {
    (void)guid;
    (void)persistent;
    if(wcscmp(name,L"BootOrder") != 0) abort();
    ++writes;
    if(writes_fail) return EFI_DEVICE_ERROR;
    if(size > sizeof(entries) || size % sizeof(entries[0])) abort();
    nentries=size/sizeof(entries[0]);
    memcpy(entries,buf,size);
    return EFI_SUCCESS;
}
#define CHECK(ok) do { if (!(ok)) { fprintf(stderr,"failed at %d: %s\n",__LINE__,#ok);exit(1);} } while(0)
static void reset(const UINT16 *items,size_t n) {
    memset(entries,0,sizeof(entries));
    memcpy(entries,items,n*sizeof(*items));
    nentries=n; writes=writes_fail=failed_read=malformed=0;
    missing=65535;
}
HEAD
awk '/^static EFI_STATUS SetBootDefault\(/ {on=1} on{print} on && /^}/ {exit}' boot/install.c
awk '/^static EFI_STATUS DeleteInvalidBootEntries\(/ {on=1} on{print} on && /^}/ {exit}' boot/install.c
cat <<'MAIN'
int main(void) {
    const UINT16 a[]={7,2,4}, b[]={7,2,7,4}, c[]={2,7,4,7};
    reset(a,3);
    CHECK(SetBootDefault(7)==EFI_SUCCESS && writes==0);
    reset(b,4);
    CHECK(SetBootDefault(7)==EFI_SUCCESS && writes==1);
    CHECK(nentries==3 && entries[0]==7 && entries[1]==2 && entries[2]==4);
    reset(c,4);
    CHECK(SetBootDefault(7)==EFI_SUCCESS && writes==1);
    CHECK(nentries==3 && entries[0]==7 && entries[1]==2 && entries[2]==4);
    reset(a,3);
    CHECK(SetBootDefault(9)==EFI_SUCCESS && writes==1);
    CHECK(nentries==4 && entries[0]==9 && entries[1]==7);
    reset(a,3);
    CHECK(SetBootDefault(0x10000)==EFI_INVALID_PARAMETER && writes==0);
    malformed=1;
    CHECK(SetBootDefault(7)==EFI_COMPROMISED_DATA && writes==0);
    reset(c,4); writes_fail=1;
    CHECK(SetBootDefault(7)==EFI_DEVICE_ERROR && writes==1);
    reset(a,3);
    CHECK(DeleteInvalidBootEntries()==EFI_SUCCESS && writes==0);
    reset(a,3); missing=2;
    CHECK(DeleteInvalidBootEntries()==EFI_SUCCESS && writes==1);
    CHECK(nentries==2 && entries[0]==7 && entries[1]==4);
    reset(a,3); failed_read=2;
    CHECK(DeleteInvalidBootEntries()==EFI_DEVICE_ERROR && writes==0 && nentries==3);
    reset(a,3); malformed=1;
    CHECK(DeleteInvalidBootEntries()==EFI_COMPROMISED_DATA && writes==0);
    puts("host firmware BootOrder regressions passed");
    return 0;
}
MAIN
} > "$work/test.c"
grep -Fq 'static EFI_STATUS SetBootDefault(' "$work/test.c"
grep -Fq 'static EFI_STATUS DeleteInvalidBootEntries(' "$work/test.c"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O1 "$work/test.c" -o "$work/test"
"$work/test"
