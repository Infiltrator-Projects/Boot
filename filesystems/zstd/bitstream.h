/*
 * Inlined reverse-bitstream decoder shared by Zstandard entropy algorithms.
 * Bit-container width, reload semantics and source boundary guards are part
 * of the decoding contract; speculative or fast reads require documented
 * padding and remaining-bit assumptions.
 */
 







































#ifndef BITSTREAM_H_MODULE
#define BITSTREAM_H_MODULE

 





 


#include "error_private.h"  
#include "mem.h"	    

 


#define STREAM_ACCUMULATOR_MIN_32 25
#define STREAM_ACCUMULATOR_MIN_64 57
#define STREAM_ACCUMULATOR_MIN ((U32)(ZSTD_32bits() ? STREAM_ACCUMULATOR_MIN_32 : STREAM_ACCUMULATOR_MIN_64))


 


typedef struct {
	size_t bitContainer;
	unsigned bitsConsumed;
	const char *ptr;
	const char *start;
} BIT_DStream_t;

typedef enum {
	BIT_DStream_unfinished = 0,
	BIT_DStream_endOfBuffer = 1,
	BIT_DStream_completed = 2,
	BIT_DStream_overflow = 3
} BIT_DStream_status;  
 

ZSTD_STATIC size_t BIT_initDStream(BIT_DStream_t *bitD, const void *srcBuffer, size_t srcSize);
ZSTD_STATIC size_t BIT_readBits(BIT_DStream_t *bitD, unsigned nbBits);
ZSTD_STATIC BIT_DStream_status BIT_reloadDStream(BIT_DStream_t *bitD);
ZSTD_STATIC unsigned BIT_endOfDStream(const BIT_DStream_t *bitD);

 










ZSTD_STATIC size_t BIT_readBitsFast(BIT_DStream_t *bitD, unsigned nbBits);
 

 


ZSTD_STATIC unsigned BIT_highbit32(register U32 val) { return 31 - __builtin_clz(val); }

 
static const unsigned BIT_mask[] = {0,       1,       3,       7,	0xF,      0x1F,     0x3F,     0x7F,      0xFF,
				    0x1FF,   0x3FF,   0x7FF,   0xFFF,    0x1FFF,   0x3FFF,   0x7FFF,   0xFFFF,    0x1FFFF,
				    0x3FFFF, 0x7FFFF, 0xFFFFF, 0x1FFFFF, 0x3FFFFF, 0x7FFFFF, 0xFFFFFF, 0x1FFFFFF, 0x3FFFFFF};  



 


 
/*
 * Initialise a reverse bit reader from the end of the supplied buffer. The
 * highest set bit in the last byte marks the end of meaningful data. It
 * determines how many leading bits of the initial accumulator to skip.
 */
ZSTD_STATIC size_t BIT_initDStream(BIT_DStream_t *bitD, const void *srcBuffer, size_t srcSize)
{
	if (srcSize < 1) {
		memset(bitD, 0, sizeof(*bitD));
		return ERROR(srcSize_wrong);
	}

	if (srcSize >= sizeof(bitD->bitContainer)) {  
		bitD->start = (const char *)srcBuffer;
		bitD->ptr = (const char *)srcBuffer + srcSize - sizeof(bitD->bitContainer);
		bitD->bitContainer = ZSTD_readLEST(bitD->ptr);
		{
			BYTE const lastByte = ((const BYTE *)srcBuffer)[srcSize - 1];
			bitD->bitsConsumed = lastByte ? 8 - BIT_highbit32(lastByte) : 0;  
			if (lastByte == 0)
				return ERROR(GENERIC);  
		}
	} else {
		bitD->start = (const char *)srcBuffer;
		bitD->ptr = bitD->start;
		bitD->bitContainer = *(const BYTE *)(bitD->start);
		switch (srcSize) {
		case 7: bitD->bitContainer += (size_t)(((const BYTE *)(srcBuffer))[6]) << (sizeof(bitD->bitContainer) * 8 - 16);
		case 6: bitD->bitContainer += (size_t)(((const BYTE *)(srcBuffer))[5]) << (sizeof(bitD->bitContainer) * 8 - 24);
		case 5: bitD->bitContainer += (size_t)(((const BYTE *)(srcBuffer))[4]) << (sizeof(bitD->bitContainer) * 8 - 32);
		case 4: bitD->bitContainer += (size_t)(((const BYTE *)(srcBuffer))[3]) << 24;
		case 3: bitD->bitContainer += (size_t)(((const BYTE *)(srcBuffer))[2]) << 16;
		case 2: bitD->bitContainer += (size_t)(((const BYTE *)(srcBuffer))[1]) << 8;
		default:;
		}
		{
			BYTE const lastByte = ((const BYTE *)srcBuffer)[srcSize - 1];
			bitD->bitsConsumed = lastByte ? 8 - BIT_highbit32(lastByte) : 0;
			if (lastByte == 0)
				return ERROR(GENERIC);  
		}
		bitD->bitsConsumed += (U32)(sizeof(bitD->bitContainer) - srcSize) * 8;
	}

	return srcSize;
}

ZSTD_STATIC size_t BIT_getUpperBits(size_t bitContainer, U32 const start) { return bitContainer >> start; }

ZSTD_STATIC size_t BIT_getMiddleBits(size_t bitContainer, U32 const start, U32 const nbBits) { return (bitContainer >> start) & BIT_mask[nbBits]; }

ZSTD_STATIC size_t BIT_getLowerBits(size_t bitContainer, U32 const nbBits) { return bitContainer & BIT_mask[nbBits]; }

 






ZSTD_STATIC size_t BIT_lookBits(const BIT_DStream_t *bitD, U32 nbBits)
{
	U32 const bitMask = sizeof(bitD->bitContainer) * 8 - 1;
	return ((bitD->bitContainer << (bitD->bitsConsumed & bitMask)) >> 1) >> ((bitMask - nbBits) & bitMask);
}

 

ZSTD_STATIC size_t BIT_lookBitsFast(const BIT_DStream_t *bitD, U32 nbBits)
{
	U32 const bitMask = sizeof(bitD->bitContainer) * 8 - 1;
	return (bitD->bitContainer << (bitD->bitsConsumed & bitMask)) >> (((bitMask + 1) - nbBits) & bitMask);
}

ZSTD_STATIC void BIT_skipBits(BIT_DStream_t *bitD, U32 nbBits) { bitD->bitsConsumed += nbBits; }

 




ZSTD_STATIC size_t BIT_readBits(BIT_DStream_t *bitD, U32 nbBits)
{
	size_t const value = BIT_lookBits(bitD, nbBits);
	BIT_skipBits(bitD, nbBits);
	return value;
}

 

ZSTD_STATIC size_t BIT_readBitsFast(BIT_DStream_t *bitD, U32 nbBits)
{
	size_t const value = BIT_lookBitsFast(bitD, nbBits);
	BIT_skipBits(bitD, nbBits);
	return value;
}

 




ZSTD_STATIC BIT_DStream_status BIT_reloadDStream(BIT_DStream_t *bitD)
{
	if (bitD->bitsConsumed > (sizeof(bitD->bitContainer) * 8))  
		return BIT_DStream_overflow;

	if (bitD->ptr >= bitD->start + sizeof(bitD->bitContainer)) {
		bitD->ptr -= bitD->bitsConsumed >> 3;
		bitD->bitsConsumed &= 7;
		bitD->bitContainer = ZSTD_readLEST(bitD->ptr);
		return BIT_DStream_unfinished;
	}
	if (bitD->ptr == bitD->start) {
		if (bitD->bitsConsumed < sizeof(bitD->bitContainer) * 8)
			return BIT_DStream_endOfBuffer;
		return BIT_DStream_completed;
	}
	{
		U32 nbBytes = bitD->bitsConsumed >> 3;
		BIT_DStream_status result = BIT_DStream_unfinished;
		if (bitD->ptr - nbBytes < bitD->start) {
			nbBytes = (U32)(bitD->ptr - bitD->start);  
			result = BIT_DStream_endOfBuffer;
		}
		bitD->ptr -= nbBytes;
		bitD->bitsConsumed -= nbBytes * 8;
		bitD->bitContainer = ZSTD_readLEST(bitD->ptr);  
		return result;
	}
}

 


ZSTD_STATIC unsigned BIT_endOfDStream(const BIT_DStream_t *DStream)
{
	return ((DStream->ptr == DStream->start) && (DStream->bitsConsumed == sizeof(DStream->bitContainer) * 8));
}

#endif  
