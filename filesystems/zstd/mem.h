/*
 * Endian-aware unaligned memory load/store helpers for compressed metadata.
 * Reads are valid only when the caller has already established that the
 * specified byte width lies within the input buffer.
 */

#ifndef MEM_H_MODULE
#define MEM_H_MODULE

#define ZSTD_STATIC static __inline __attribute__((unused))

typedef uint8_t BYTE;
typedef uint16_t U16;
typedef int16_t S16;
typedef uint32_t U32;
typedef int32_t S32;
typedef uint64_t U64;
typedef int64_t S64;
typedef ptrdiff_t iPtrDiff;
typedef uintptr_t uPtrDiff;

ZSTD_STATIC unsigned ZSTD_32bits(void) { return sizeof(size_t) == 4; }
ZSTD_STATIC unsigned ZSTD_64bits(void) { return sizeof(size_t) == 8; }

ZSTD_STATIC U16 ZSTD_readLE16(const void *memPtr) { return get_unaligned_le16(memPtr); }

ZSTD_STATIC void ZSTD_writeLE16(void *memPtr, U16 val) { put_unaligned_le16(val, memPtr); }

ZSTD_STATIC U32 ZSTD_readLE24(const void *memPtr) { return ZSTD_readLE16(memPtr) + (((const BYTE *)memPtr)[2] << 16); }

ZSTD_STATIC U32 ZSTD_readLE32(const void *memPtr) { return get_unaligned_le32(memPtr); }

ZSTD_STATIC U64 ZSTD_readLE64(const void *memPtr) { return get_unaligned_le64(memPtr); }

/*
 * The bitstream reader refills its accumulator one size_t at a time. Select a
 * 32-bit or 64-bit little-endian load to match the accumulator width on this
 * build target.
 */
ZSTD_STATIC size_t ZSTD_readLEST(const void *memPtr)
{
	if (ZSTD_32bits())
		return (size_t)ZSTD_readLE32(memPtr);
	else
		return (size_t)ZSTD_readLE64(memPtr);
}

#endif  
