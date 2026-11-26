/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_COMMON_V2_ELF_H
#define BOOT_COMMON_V2_ELF_H
#include <stddef.h>
#include <stdint.h>

/* Restricted AMD64 SysV ELF profile. The host must own the immutable input
 * buffer throughout loading. Authentication must verify that exact buffer
 * against an independently trusted policy, not a digest stored beside it.
 * reserve supplies page-aligned RW/NX memory; protect changes page attributes
 * and release destroys the complete reservation, including on partial failure.
 * None of these callbacks is a Common filesystem ABI; this is BOOT-private. */
typedef struct {
    void *context;
    int (*authenticate)(void *, const void *, size_t);
    void *(*reserve)(void *, size_t);
    int (*protect)(void *, void *, size_t, unsigned);
    void (*release)(void *, void *, size_t);
} BootV2ElfHost;

enum { BOOT_V2_ELF_X = 1, BOOT_V2_ELF_W = 2, BOOT_V2_ELF_R = 4 };
#define BOOT_V2_ELF_MAX_SEGMENTS 128u
typedef struct {
    size_t offset, bytes, file_bytes;
    unsigned flags;
} BootV2ElfRegion;
typedef struct {
    void *base;
    size_t bytes;
    void *entry;
    unsigned region_count;
    size_t relro_start, relro_end;
    BootV2ElfRegion regions[BOOT_V2_ELF_MAX_SEGMENTS];
} BootV2ElfImage;
/* Check a pointer against actual loaded segment bytes and final permissions,
 * not just the reservation. Executable checks also exclude BSS and padding. */
int boot_v2_elf_contains(const BootV2ElfImage *, uintptr_t, size_t,
                         unsigned required, unsigned forbidden);

/* Returns zero only after authentication, mapping, relocation, exact export
 * lookup and memory protection. Never calls module code. Caller must still
 * validate the canonical V2 descriptor before exposing a volume. */
int boot_v2_elf_load(const BootV2ElfHost *, const void *, size_t, BootV2ElfImage *);
void boot_v2_elf_unload(const BootV2ElfHost *, BootV2ElfImage *);
#endif
