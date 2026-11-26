/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_COMMON_V2_MEMORY_H
#define BOOT_COMMON_V2_MEMORY_H
#include "common_v2_elf.h"

/* BOOT-private page protection backend. The firmware implementation maps
 * these three access restrictions to EFI_MEMORY_RP, RO and XP. */
enum { BOOT_V2_MEM_RP=1, BOOT_V2_MEM_RO=2, BOOT_V2_MEM_XP=4 };
typedef struct {
    void *context;
    int (*allocate)(void *,size_t,uintptr_t *);
    int (*free)(void *,uintptr_t,size_t);
    int (*set)(void *,uintptr_t,size_t,unsigned);
    int (*clear)(void *,uintptr_t,size_t,unsigned);
    int (*get)(void *,uintptr_t,size_t,unsigned *);
} BootV2MemoryBackend;
typedef struct {
    BootV2MemoryBackend backend;
    void *trust_context;
    int (*authenticate)(void *,const void *,size_t);
    uintptr_t base;
    size_t bytes;
    unsigned allocated,quarantined;
} BootV2Memory;
/* One memory context per module. Authentication is mandatory and supplied
 * by the trust policy; absent services disable loading, never weaken it. */
int boot_v2_memory_host(BootV2Memory *,const BootV2MemoryBackend *,void *,
                        int (*)(void *,const void *,size_t),BootV2ElfHost *);
#endif
