#!/usr/bin/env bash
# BOOT / Host source regressions
#
# Compiles selected production functions with small host-only fixtures for
# boundary and ownership checks.
#
# Isolated host stubs do not reproduce EFI firmware, filesystem disks or
# physical boot transitions.

set -euo pipefail
cd "$(dirname "$0")/.."
# Normal Linux fallback launches Plymouth where available; do not silence
# recovery mode or override an explicit distribution kernel options file.
grep -Fq 'ro root=%s quiet splash' boot/config.c
grep -Fq '%s root=/dev/disk/by-partuuid/%s quiet splash' boot/config.c
grep -Fq 'ro root=%s single' boot/config.c
grep -Fq '%s root=/dev/disk/by-partuuid/%s single' boot/config.c

work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
{
cat <<'HEAD'
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
typedef size_t UINTN;
typedef ptrdiff_t INTN;
typedef uint8_t UINT8;
typedef void VOID;
#define IN
#define CompareMem memcmp
typedef int fsw_status_t;
#define FSW_SUCCESS 0
#define RECOVER_CACHE_SIZE 8
struct fsw_volume { int unused; };
struct fsw_volume_stat { uint64_t total_bytes, free_bytes; };
struct fsw_btrfs_device_desc { uint64_t id; struct fsw_volume *dev; };
struct fsw_btrfs_recover_cache { void *buffer; };
struct fsw_btrfs_volume {
    struct fsw_volume g;
    uint64_t total_bytes, bytes_used;
    int is_master;
    unsigned n_devices_attached;
    struct fsw_btrfs_device_desc *devices_attached;
    void *extent;
    struct fsw_btrfs_recover_cache *rcache;
};
static void *released[RECOVER_CACHE_SIZE + 8];
static unsigned nreleased, nunmounted, nmasters;
static void FreePool(void *p) {
    unsigned i;
    if (!p || nreleased >= RECOVER_CACHE_SIZE + 8) abort();
    for (i=0; i<nreleased; ++i) {
        if (released[i] == p) { fputs("duplicate cache free\n",stderr); abort(); }
    }
    released[nreleased++]=p;
    free(p);
}
static void fsw_unmount(struct fsw_volume *p) { if (!p) abort(); ++nunmounted; }
static void master_uuid_remove(struct fsw_btrfs_volume *p) { if (!p) abort(); ++nmasters; }
HEAD
awk '/^INTN FindMem\(/ { on=1 } on {print} on && /^}/ {exit}' boot/lib.c
awk '/^static void fsw_btrfs_volume_free\(/ { on=1 } on {print} on && /^}/ {exit}' filesystems/fsw_btrfs.c
awk '/^static fsw_status_t fsw_btrfs_volume_stat\(/ { on=1 } on {print} on && /^}/ {exit}' filesystems/fsw_btrfs.c
cat <<'MAIN'
#define CHECK(x) do { if (!(x)) {fprintf(stderr,"failed: %s\n",#x);exit(1);} } while(0)
int main(void) {
    const UINT8 data[]={1,2,0,4,5,6}, needle[]={0,4,5}, miss[]={9};
    struct fsw_btrfs_volume v={0};
    struct fsw_volume a={0}, b={0};
    struct fsw_volume_stat st={0};
    unsigned i;
    CHECK(FindMem((void *)data,6,(void *)needle,3)==2);
    CHECK(FindMem((void *)data,6,(void *)(data+5),1)==5);
    CHECK(FindMem((void *)data,6,(void *)data,6)==0);
    CHECK(FindMem((void *)data,6,(void *)data,7)==-1);
    CHECK(FindMem((void *)data,6,(void *)miss,1)==-1);
    CHECK(FindMem((void *)data,6,(void *)data,0)==0);
    CHECK(FindMem(NULL,6,(void *)miss,1)==-1);
    v.total_bytes=100; v.bytes_used=27;
    CHECK(fsw_btrfs_volume_stat(&v.g,&st)==FSW_SUCCESS);
    CHECK(st.total_bytes==100 && st.free_bytes==73);
    v.bytes_used=150;
    CHECK(fsw_btrfs_volume_stat(&v.g,&st)==FSW_SUCCESS);
    CHECK(st.free_bytes==0);
    v.is_master=1; v.n_devices_attached=3;
    v.devices_attached=calloc(3,sizeof(*v.devices_attached));
    CHECK(v.devices_attached!=NULL);
    v.devices_attached[1].dev=&a; v.devices_attached[2].dev=&b;
    v.extent=malloc(8);
    v.rcache=calloc(RECOVER_CACHE_SIZE,sizeof(*v.rcache));
    CHECK(v.extent && v.rcache);
    for (i=0;i<RECOVER_CACHE_SIZE;i++) {
        v.rcache[i].buffer=malloc(16);
        CHECK(v.rcache[i].buffer!=NULL);
    }
    fsw_btrfs_volume_free(&v.g);
    CHECK(nreleased==RECOVER_CACHE_SIZE+3);
    CHECK(nunmounted==2 && nmasters==1);
    puts("host source regressions passed");
    return 0;
}
MAIN
} > "$work/test.c"
grep -Fq 'INTN FindMem(' "$work/test.c"
grep -Fq 'static void fsw_btrfs_volume_free(' "$work/test.c"
grep -Fq 'static fsw_status_t fsw_btrfs_volume_stat(' "$work/test.c"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O1 "$work/test.c" -o "$work/test"
"$work/test"

# Exercise actual production code, not a copied decoder. The retired Python
# generator must not reappear in the source or native release archive.
test ! -e filesystems/mk_fsw_strfunc.py
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O1 -g -DHOST_POSIX \
    -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
    -I filesystems filesystems/fsw_lib.c tests/fsw-string-regressions.c \
    -o "$work/fsw-string-test"
"$work/fsw-string-test"

# Check the actual firmware PNG allocator's ownership and size arithmetic
# against an instrumented host allocator, including forced failures.
{
cat <<'PNG_HEAD'
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static unsigned active, attempts, fail_next;
static void *AllocateZeroPool(size_t bytes) {
    void *p;
    ++attempts;
    if (fail_next) { fail_next=0; return NULL; }
    p=calloc(1,bytes);
    if (p) ++active;
    return p;
}
static void MyFreePool(void *p) {
    assert(p && active);
    --active;
    free(p);
}
#define MyCopyMem memcpy
PNG_HEAD
# Extract allocator entry points and their private size helper together.
sed -n '/^void\* lodepng_malloc(/,/^int MyStrlen(/{ /^int MyStrlen(/!p; }' libeg/lodepng_xtra.c
cat <<'PNG_MAIN'
int main(void) {
    unsigned char *a=lodepng_malloc(4), *b;
    assert(a && active==1);
    memcpy(a,"BOOT",4);
    fail_next=1;
    assert(lodepng_realloc(a,8)==NULL);
    assert(active==1 && memcmp(a,"BOOT",4)==0);
    b=lodepng_realloc(a,8);
    assert(b && active==1 && memcmp(b,"BOOT",4)==0 && b[7]==0);
    a=lodepng_realloc(b,2);
    assert(a && active==1 && memcmp(a,"BO",2)==0);
    lodepng_free(a);
    assert(active==0);
    a=lodepng_realloc(NULL,3);
    assert(a && active==1);
    lodepng_free(a);
    unsigned before=attempts;
    assert(lodepng_malloc(SIZE_MAX)==NULL && attempts==before);
    lodepng_free(NULL);
    assert(active==0);
    puts("PNG allocator ownership regressions passed");
    return 0;
}
PNG_MAIN
} > "$work/png-allocator.c"
"${CC:-cc}" -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    -fno-omit-frame-pointer -fno-pie -no-pie "$work/png-allocator.c" -o "$work/png-allocator"
"$work/png-allocator"

# Exercise the production driver dispatcher with instrumented child makefiles.
# These verify the selected backend and shared-object invalidation, not EFI
# compilation. The firmware build and smoke tests qualify that separately.
dispatch="$work/driver-dispatch"
mkdir -p "$dispatch/test"
cp filesystems/Makefile "$dispatch/Makefile"
for profile in tiano gnuefi; do
    case "$profile" in
        tiano) object=fsw_efi.obj ;;
        gnuefi) object=fsw_efi.o ;;
    esac
    printf 'all:\n\t@test ! -e %s\n\t@echo %s:$(DRIVERNAME) >> trace\n\t@touch %s\n' \
        "$object" "$profile" "$object" > "$dispatch/Make.$profile"
done
printf 'clean:\n\t@echo clean >> ../trace\n' > "$dispatch/test/Makefile"
# Deliberate filename collisions must not suppress any phony build target.
for driver in ext2 ext4 reiserfs iso9660 hfs btrfs ntfs; do
    touch "$dispatch/$driver" "$dispatch/${driver}_gnuefi"
done
check_dispatch() {
    local profile=$1 expected=$2
    shift 2
    rm -f "$dispatch/trace"
    make -s -C "$dispatch" -j16 "$@"
    diff -u <(for driver in $expected; do echo "$profile:$driver"; done) "$dispatch/trace"
}
standard='ext2 ext4 reiserfs iso9660 hfs btrfs'
check_dispatch tiano "$standard"
check_dispatch tiano "$standard" all
check_dispatch gnuefi "$standard" gnuefi
check_dispatch gnuefi "$standard" all_gnuefi
check_dispatch tiano ntfs ntfs
check_dispatch gnuefi ntfs ntfs_gnuefi
check_dispatch gnuefi 'ext2 hfs' ext2_gnuefi hfs_gnuefi
rm -f "$dispatch/trace"
make -s -C "$dispatch" clean
test "$(cat "$dispatch/trace")" = clean
test ! -e "$dispatch/fsw_efi.o" && test ! -e "$dispatch/fsw_efi.obj"
echo 'Driver build dispatch regressions passed'
