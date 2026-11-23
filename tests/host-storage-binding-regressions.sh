#!/usr/bin/env bash
# Exercise the production EFI storage-connection function against controlled
# protocol handles. The fixture proves that filesystem binding never recurses
# into unrelated firmware controllers; it is not a physical firmware test.
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
#include <wchar.h>
typedef unsigned long UINTN;
typedef uint64_t UINT64;
typedef uint32_t UINT32;
typedef long EFI_STATUS;
typedef int BOOLEAN;
typedef void VOID;
typedef wchar_t CHAR16;
typedef int EFI_GUID;
typedef void *EFI_HANDLE;
typedef struct { BOOLEAN MediaPresent; } EFI_BLOCK_IO_MEDIA;
typedef struct { EFI_BLOCK_IO_MEDIA *Media; } MY_EFI_BLOCK_IO_PROTOCOL;
typedef struct { int dummy; } MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;
typedef struct { int dummy; } EFI_DEVICE_PATH;
#define TRUE 1
#define FALSE 0
#define EFI_SUCCESS 0
#define EFI_NOT_FOUND 14
#define EFI_DEVICE_ERROR 7
#define EFI_ERROR(s) ((s)!=EFI_SUCCESS)
#define ByProtocol 1
#define BOOT_PROGRESS_ACTIVE 1
#define BOOT_PROGRESS_OK 2
#define BOOT_PROGRESS_ERROR 3
#define LOG_LINE_NORMAL 0
#define LOG(...) ((void)0)
typedef struct {
 EFI_STATUS (*LocateHandleBuffer)(int,EFI_GUID*,VOID*,UINTN*,EFI_HANDLE**);
 EFI_STATUS (*HandleProtocol)(EFI_HANDLE,EFI_GUID*,VOID**);
 EFI_STATUS (*ConnectController)(EFI_HANDLE,EFI_HANDLE*,VOID*,BOOLEAN);
} BOOT_SERVICES;
#define boot_call5_wrapper(fn,a,b,c,d,e) fn(a,b,c,d,e)
#define boot_call3_wrapper(fn,a,b,c) fn(a,b,c)
#define boot_call4_wrapper(fn,a,b,c,d) fn(a,b,c,d)
static EFI_GUID gMyEfiBlockIoProtocolGuid=1;
static EFI_GUID gMyEfiSimpleFileSystemProtocolGuid=2;
static struct { BOOLEAN StartupDiagnostics; } GlobalConfig = { FALSE };
static EFI_BLOCK_IO_MEDIA media[]={{TRUE},{TRUE},{FALSE}};
static MY_EFI_BLOCK_IO_PROTOCOL block[]={{&media[0]},{&media[1]},{&media[2]},{NULL}};
static MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL fs;
static unsigned connects, nonrecursive, query_count;
static EFI_STATUS mock_locate(int by,EFI_GUID *guid,VOID *key,UINTN *count,EFI_HANDLE **out) {
 UINTN i;
 (void)key;
 if (by!=ByProtocol || guid!=&gMyEfiBlockIoProtocolGuid) abort();
 ++query_count;
 *count=4;
 *out=calloc(*count,sizeof(**out));
 if (!*out) abort();
 for (i=0;i<*count;++i) (*out)[i]=(EFI_HANDLE)(uintptr_t)(i+1);
 return EFI_SUCCESS;
}
static EFI_STATUS mock_protocol(EFI_HANDLE handle,EFI_GUID *guid,VOID **out) {
 UINTN index=(UINTN)(uintptr_t)handle;
 if (!index || index>4) abort();
 if (guid==&gMyEfiBlockIoProtocolGuid) { *out=&block[index-1]; return EFI_SUCCESS; }
 if (guid==&gMyEfiSimpleFileSystemProtocolGuid && index==1) {
   *out=&fs; return EFI_SUCCESS;
 }
 return EFI_NOT_FOUND;
}
static EFI_STATUS mock_connect(EFI_HANDLE handle,EFI_HANDLE *drv,VOID *remaining,BOOLEAN recursive) {
 if ((UINTN)(uintptr_t)handle!=2 || drv || remaining || recursive) abort();
 ++connects;
 ++nonrecursive;
 return EFI_NOT_FOUND;
}
static BOOT_SERVICES services={mock_locate,mock_protocol,mock_connect};
static BOOT_SERVICES *BS=&services;
static UINT64 BootDiagTicks(void) { return 0; }
static UINT64 BootDiagElapsedMs(UINT64 stamp) { (void)stamp; return 0; }
static void BootDiagFlush(void) {}
static void BootStartupProgressReport(CHAR16 *text,int status) {(void)text;(void)status;}
static CHAR16 *PoolPrint(const CHAR16 *fmt,...) {(void)fmt;return calloc(8,sizeof(CHAR16));}
static EFI_DEVICE_PATH *DevicePathFromHandle(EFI_HANDLE handle) {(void)handle;return NULL;}
static CHAR16 *DevicePathToStr(EFI_DEVICE_PATH *path) {(void)path;return NULL;}
static void MyFreePool(void *p) { free(p); }
HEAD
awk '/^EFI_STATUS ConnectAllDriversToAllControllers\(VOID\)/ {on=1} on {print} on && /^}/ {exit}' boot/driver_support.c
cat <<'TEST'
int main(void) {
 if (ConnectAllDriversToAllControllers()!=EFI_SUCCESS ||
     query_count!=1 || connects!=1 || nonrecursive!=1) {
   fprintf(stderr,"incorrect bounded block-device binding: queries=%u connects=%u nonrecursive=%u\n",
           query_count,connects,nonrecursive);
   return 1;
 }
 puts("EFI storage binding regression passed");
 return 0;
}
TEST
} > "$work/test.c"
grep -Fq 'EFI_STATUS ConnectAllDriversToAllControllers(VOID)' "$work/test.c"
"${CC:-cc}" -std=c11 -Wall -Wextra -O1 "$work/test.c" -o "$work/test"
"$work/test"
