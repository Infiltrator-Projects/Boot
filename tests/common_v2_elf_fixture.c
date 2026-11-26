/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Loader-only fixture. This deliberately is NOT a filesystem descriptor. */
static const unsigned marker = 0x12345678u;
#ifdef TEST_RELRO_ONLY
static const struct { unsigned value; const unsigned *pointer; } descriptor={0x12345678u,&marker};
__attribute__((visibility("default")))
const void *infiltrator_filesystem_api_v2(void) { return &descriptor.value; }
#else
static const unsigned *volatile marker_pointer = &marker;
__attribute__((visibility("default")))
const void *infiltrator_filesystem_api_v2(void) { return marker_pointer; }
#endif
#ifdef TEST_MIXED_ABI
__attribute__((visibility("default")))
const void *infiltrator_filesystem_api_v1(void) { return marker_pointer; }
#endif
