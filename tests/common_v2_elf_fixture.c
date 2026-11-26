/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Loader-only fixture. This deliberately is NOT a filesystem descriptor. */
static const unsigned marker = 0x12345678u;
static const unsigned *volatile marker_pointer = &marker;
__attribute__((visibility("default")))
const void *infiltrator_filesystem_api_v2(void) { return marker_pointer; }
#ifdef TEST_MIXED_ABI
__attribute__((visibility("default")))
const void *infiltrator_filesystem_api_v1(void) { return marker_pointer; }
#endif
