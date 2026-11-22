#!/usr/bin/env bash
# Host-only regression: compile the shipping LVM2 text parser and mapping
# logic against synthetic, non-disk volume-group metadata.
set -euo pipefail
cd "$(dirname "$0")/.."
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
{
cat <<'HOST'
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned char UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef size_t UINTN;
typedef char CHAR8;
typedef void VOID;
typedef int BOOLEAN;
#define TRUE 1
#define FALSE 0
#define LVM_PVS_MAX 16
#define LVM_SEGS_MAX 64
typedef struct { const CHAR8 *Current, *End; } LVM_SPAN;
typedef struct { UINT64 Bytes; CHAR8 Id[33]; } LVM_PV;
typedef struct { UINT64 Start, Count, Physical; UINTN PvIndex; } LVM_SEGMENT;
typedef struct { UINT64 Sectors; UINTN SegCount; LVM_SEGMENT Segments[LVM_SEGS_MAX]; } LVM_LV;
typedef struct { CHAR8 Alias[64]; UINTN PvIndex; UINT64 PeStart, PeCount; } LVM_ALIAS;
static LVM_PV LvmPvs[LVM_PVS_MAX];
static UINTN LvmPvCount = 1;
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "FAILED line %d: %s\n", __LINE__, #cond); return 1; \
} } while (0)
HOST
awk '/^static UINT32 LvmU32\(/ {copy=1} /^static EFI_STATUS LvmReadPv\(/ {copy=0} copy {print}' boot/lvm2.c
awk '/^static BOOLEAN LvmAliases\(/ {copy=1} /^#define LVM_OWNER\(/ {copy=0} copy {print}' boot/lvm2.c
cat <<'MAIN'
int main(void) {
    const char *valid =
        "contents = \"Text Format Volume Group\"\n"
        "version = 1\n"
        "vgmint {\n"
        "  id = \"Q23456-1234-Abcd-0000-1234-ABCDEF1234\"\n"
        "  seqno = 9\n"
        "  extent_size = 8192\n"
        "  physical_volumes {\n"
        "    pv0 { id = \"ZBW5qW-dXF2-0bGw-ZCad-2RlV-phwu-1c1RFt\"\n"
        "      pe_start = 2048 pe_count = 200 }\n"
        "  }\n"
        "  logical_volumes {\n"
        "    root {\n"
        "      id = \"hGlUwi-zsBg-39FF-do88-pHxY-8XA2-9WKIiA\"\n"
        "      status = [\"READ\", \"WRITE\", \"VISIBLE\"]\n"
        "      segment_count = 2\n"
        "      segment1 { start_extent = 0 extent_count = 4\n"
        "        type = \"striped\" stripe_count = 1 stripes = [\"pv0\", 0] }\n"
        "      segment2 { start_extent = 4 extent_count = 2\n"
        "        type = \"striped\" stripe_count = 1 stripes = [\"pv0\", 4] }\n"
        "    }\n"
        "  }\n"
        "}\n";
    LVM_SPAN whole = {valid, valid + strlen(valid)}, vg, physical, logical, lv;
    LVM_ALIAS aliases[LVM_PVS_MAX];
    LVM_LV output = {0};
    CHAR8 name[128], id[33], field[128];
    UINT64 size;
    UINTN count = 0;
    CHECK(LvmNormalizeId("ZBW5qW-dXF2-0bGw-ZCad-2RlV-phwu-1c1RFt", id));
    strcpy(LvmPvs[0].Id, id);
    LvmPvs[0].Bytes = (UINT64)512 * 8192 * 200;
    CHECK(LvmNextSection(&whole, name, sizeof(name), &vg));
    CHECK(strcmp(name,"vgmint") == 0);
    CHECK(LvmGetNumber(vg,"extent_size",&size) && size == 8192);
    CHECK(LvmFindSection(vg,"physical_volumes",&physical));
    CHECK(LvmAliases(vg,aliases,&count) && count == 1);
    CHECK(aliases[0].PvIndex == 0 && aliases[0].PeStart == 2048);
    CHECK(LvmFindSection(vg,"logical_volumes",&logical));
    CHECK(LvmNextSection(&logical,name,sizeof(name),&lv));
    CHECK(strcmp(name,"root") == 0);
    CHECK(LvmHasFlag(lv,"status","VISIBLE"));
    CHECK(LvmFindField(lv,"id",field,sizeof(field)));
    CHECK(LvmParseSegments(lv,8192,aliases,count,&output));
    CHECK(output.Sectors == 6 * 8192);
    CHECK(output.SegCount == 2);
    CHECK(output.Segments[0].Start == 0);
    CHECK(output.Segments[0].Physical == 2048);
    CHECK(output.Segments[1].Start == 4 * 8192);
    CHECK(output.Segments[1].Physical == 2048 + 4 * 8192);
    aliases[0].PvIndex = LvmPvCount;
    memset(&output,0,sizeof(output));
    CHECK(!LvmParseSegments(lv,8192,aliases,count,&output));
    aliases[0].PvIndex = 0;
    aliases[0].PeCount = 2;
    memset(&output,0,sizeof(output));
    CHECK(!LvmParseSegments(lv,8192,aliases,count,&output));
    puts("BOOT LVM2 parser and linear extent mapping regression passed");
    return 0;
}
MAIN
} > "$work/lvm2-test.c"
cc -std=c11 -Wall -Wextra -Werror -Wno-unused-function -O2 \
    "$work/lvm2-test.c" -o "$work/lvm2-test"
"$work/lvm2-test"
