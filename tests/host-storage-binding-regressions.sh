#!/usr/bin/env bash
# Exercise the production EFI storage and virtual-LVM binding implementations
# with host protocol mocks. Never connects physical firmware or writes a disk.
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
/* Evaluate diagnostic arguments in tests, even though no log is emitted. */
static void MockLog(int level,int category,const CHAR16 *format,...) {
 (void)level;(void)category;(void)format;
}
#define LOG(...) MockLog(__VA_ARGS__)
typedef struct BOOT_FS_DRIVER_BINDING {
 EFI_STATUS (*Supported)(struct BOOT_FS_DRIVER_BINDING*,EFI_HANDLE,VOID*);
 EFI_STATUS (*Start)(struct BOOT_FS_DRIVER_BINDING*,EFI_HANDLE,VOID*);
} BOOT_EFI_DRIVER_BINDING_PROTOCOL;
typedef struct BOOT_FS_DRIVER_NODE {
    struct BOOT_FS_DRIVER_NODE *Next;
    EFI_HANDLE Image;
    CHAR16 *Name;
} BOOT_FS_DRIVER_NODE;
static BOOT_FS_DRIVER_NODE fs_driver={NULL,(EFI_HANDLE)(uintptr_t)100,L"ext4"};
static BOOT_FS_DRIVER_NODE *BootFsDrivers=&fs_driver;
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
static EFI_GUID gMyEfiDriverBindingProtocolGuid=3;
static struct { BOOLEAN StartupDiagnostics; } GlobalConfig = { FALSE };

/* Mirror the production early-volume inventory used by the exclusion gate.
 * The LVM mocks (handles 5/6) are published only AFTER the initial scan. */
#define FS_TYPE_EXT4 7
#define FS_TYPE_XFS 11
typedef struct { EFI_HANDLE DeviceHandle; UINT32 FSType; } BOOT_VOLUME;
static BOOT_VOLUME recognized_candidate={(EFI_HANDLE)(uintptr_t)2,FS_TYPE_EXT4};
static BOOT_VOLUME unsupported_candidate={(EFI_HANDLE)(uintptr_t)4,FS_TYPE_XFS};
static BOOT_VOLUME *Volumes[]={&recognized_candidate,&unsupported_candidate};
static UINTN VolumesCount=2;
static BOOLEAN exclusion_enabled=FALSE;
static BOOLEAN BootVolumeExcluded(BOOT_VOLUME *volume) {
 return exclusion_enabled && volume==&recognized_candidate;
}
/* Host mock of the production signature-to-reader capability predicate. */
static BOOLEAN BootHasFilesystemReader(UINT32 fs) {
 return fs==FS_TYPE_EXT4;
}
static EFI_BLOCK_IO_MEDIA media[]={{TRUE},{TRUE},{FALSE},{TRUE},{TRUE}};
static MY_EFI_BLOCK_IO_PROTOCOL block[]={{&media[0]},{&media[1]},
                                         {&media[2]},{&media[3]},
                                         {&media[3]},{&media[4]}};
static MY_EFI_SIMPLE_FILE_SYSTEM_PROTOCOL fs;
static unsigned connects, unsupported, mounts, query_count, probed;
static BOOLEAN root_mounted=FALSE;
static EFI_STATUS mock_supported(BOOT_EFI_DRIVER_BINDING_PROTOCOL *drv,
                                  EFI_HANDLE handle,VOID *path) {
 (void)drv;(void)path;
 if ((uintptr_t)handle!=5 && (uintptr_t)handle!=6) abort();
 ++probed;
 return EFI_SUCCESS;
}
static EFI_STATUS mock_start(BOOT_EFI_DRIVER_BINDING_PROTOCOL *drv,
                            EFI_HANDLE handle,VOID *path) {
 (void)drv;(void)path;
 if ((uintptr_t)handle==5) {++mounts;root_mounted=TRUE;return EFI_SUCCESS;}
 if ((uintptr_t)handle==6) {++unsupported;return EFI_NOT_FOUND;}
 abort();
}
static BOOT_EFI_DRIVER_BINDING_PROTOCOL binding={mock_supported,mock_start};
static EFI_STATUS mock_locate(int by,EFI_GUID *guid,VOID *key,
                             UINTN *count,EFI_HANDLE **out) {
 UINTN i;
 (void)key;
 if (by!=ByProtocol || guid!=&gMyEfiBlockIoProtocolGuid) abort();
 ++query_count;
 *count=6;
 *out=calloc(*count,sizeof(**out));
 if (!*out) abort();
 for (i=0;i<*count;++i) (*out)[i]=(EFI_HANDLE)(uintptr_t)(i+1);
 return EFI_SUCCESS;
}
static EFI_STATUS mock_protocol(EFI_HANDLE handle,EFI_GUID *guid,VOID **out) {
 UINTN index=(UINTN)(uintptr_t)handle;
 if (index==100 && guid==&gMyEfiDriverBindingProtocolGuid) {
   *out=&binding; return EFI_SUCCESS;
 }
 if (!index || index>6) abort();
 if (guid==&gMyEfiBlockIoProtocolGuid) { *out=&block[index-1]; return EFI_SUCCESS; }
 if (guid==&gMyEfiSimpleFileSystemProtocolGuid &&
     (index==1 || (index==5 && root_mounted))) {
   *out=&fs; return EFI_SUCCESS;
 }
 return EFI_NOT_FOUND;
}
static EFI_STATUS mock_connect(EFI_HANDLE handle,EFI_HANDLE *drv,
                               VOID *remaining,BOOLEAN recursive) {
 if ((UINTN)(uintptr_t)handle!=2 || drv || remaining || recursive) abort();
 ++connects;
 return EFI_NOT_FOUND;
}
static BOOLEAN LvmIsPublishedHandle(EFI_HANDLE handle) {
 return (uintptr_t)handle==5 || (uintptr_t)handle==6;
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
awk '/^static EFI_STATUS BootBindVirtualFilesystem\(EFI_HANDLE VolumeHandle\)/ {on=1} on {print} on && /^}/ {exit}' boot/driver_support.c
awk '/^EFI_STATUS ConnectAllDriversToAllControllers\(VOID\)/ {on=1} on {print} on && /^}/ {exit}' boot/driver_support.c
cat <<'TEST'
int main(void) {
 /* Handle 2 is ext4 and eligible; handle 4 is unsupported XFS and
  * must never reach ConnectController. Neither is hardcoded in BOOT. */
 if (ConnectAllDriversToAllControllers()!=EFI_SUCCESS ||
     query_count!=1 || connects!=1 || probed!=2 || mounts!=1 ||
     unsupported!=1 || !root_mounted) {
   fprintf(stderr,"incorrect storage/LVM binding: queries=%u connects=%u probes=%u mounts=%u unsupported=%u\n",
           query_count,connects,probed,mounts,unsupported);
   return 1;
 }
 /* A configured exclusion must skip a normally probed controller,
  * without skipping published LVM handles or other usable partitions. */
 exclusion_enabled=TRUE;
 query_count=connects=unsupported=mounts=probed=0;
 root_mounted=FALSE;
 if (ConnectAllDriversToAllControllers()!=EFI_SUCCESS ||
     query_count!=1 || connects!=0 || probed!=2 || mounts!=1 ||
     unsupported!=1 || !root_mounted) {
   fprintf(stderr,"excluded controller was probed or LVM binding regressed: connects=%u probes=%u mounts=%u\n",
           connects,probed,mounts);
   return 1;
 }
 puts("EFI storage and explicit virtual-LVM binding regressions passed");
 return 0;
}
TEST
} > "$work/test.c"
grep -Fq 'static EFI_STATUS BootBindVirtualFilesystem(EFI_HANDLE VolumeHandle)' "$work/test.c"
grep -Fq 'EFI_STATUS ConnectAllDriversToAllControllers(VOID)' "$work/test.c"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O1 "$work/test.c" -o "$work/test"
"$work/test"
