/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BOOT_COMMON_V2_TRUST_H
#define BOOT_COMMON_V2_TRUST_H
#include <stddef.h>
#include <stdint.h>
/* SHA-256 over exact raw ELF bytes, not a PE Authenticode digest.
 * The database parser expects EFI_SIGNATURE_LIST wire format, not a
 * manifest supplied by an untrusted filesystem. */
void boot_v2_sha256(const void *,size_t,uint8_t digest[32]);
/* Returns 0 only if a valid allow database contains the exact digest
 * and a valid revocation database does not. Unknown signature types are
 * not interpreted as raw SHA-256 hashes. Malformed lists reject entirely. */
int boot_v2_trust_digest(const void *,size_t,const void *,size_t,const uint8_t[32]);
#endif
