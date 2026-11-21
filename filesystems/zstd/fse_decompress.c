/*
 * Finite State Entropy table construction and bitstream decompression.
 * Normalised symbol counts, table logarithms and source stream lengths are
 * external metadata; workspace capacity and table indices constrain safe
 * execution in the freestanding filesystem decoder.
 */
 






































 


#define FORCE_INLINE static __always_inline

 


#include "bitstream.h"
#include "fse.h"

 


#define FSE_STATIC_ASSERT(c)                                   \
	{                                                      \
		enum { FSE_static_assert = 1 / (int)(!!(c)) }; \
	}  


 


 





 
#ifndef FSE_FUNCTION_EXTENSION
#error "FSE_FUNCTION_EXTENSION must be defined"
#endif
#ifndef FSE_FUNCTION_TYPE
#error "FSE_FUNCTION_TYPE must be defined"
#endif

 
#define FSE_CAT(X, Y) X##Y
#define FSE_FUNCTION_NAME(X, Y) FSE_CAT(X, Y)
#define FSE_TYPE_NAME(X, Y) FSE_CAT(X, Y)

 

/*
 * Constructs an entropy decoding table from normalised symbol frequencies.
 * Frequency sums and symbol maxima must be consistent with its table log.
 */
size_t FSE_buildDTable_wksp(FSE_DTable *dt, const short *normalizedCounter, unsigned maxSymbolValue, unsigned tableLog, void *workspace, size_t workspaceSize)
{
	void *const tdPtr = dt + 1;  
	FSE_DECODE_TYPE *const tableDecode = (FSE_DECODE_TYPE *)(tdPtr);
	U16 *symbolNext = (U16 *)workspace;

	U32 const maxSV1 = maxSymbolValue + 1;
	U32 const tableSize = 1 << tableLog;
	U32 highThreshold = tableSize - 1;

	 
	if (workspaceSize < sizeof(U16) * (FSE_MAX_SYMBOL_VALUE + 1))
		return ERROR(tableLog_tooLarge);
	if (maxSymbolValue > FSE_MAX_SYMBOL_VALUE)
		return ERROR(maxSymbolValue_tooLarge);
	if (tableLog > FSE_MAX_TABLELOG)
		return ERROR(tableLog_tooLarge);

	 
	{
		FSE_DTableHeader DTableH;
		DTableH.tableLog = (U16)tableLog;
		DTableH.fastMode = 1;
		{
			S16 const largeLimit = (S16)(1 << (tableLog - 1));
			U32 s;
			for (s = 0; s < maxSV1; s++) {
				if (normalizedCounter[s] == -1) {
					tableDecode[highThreshold--].symbol = (FSE_FUNCTION_TYPE)s;
					symbolNext[s] = 1;
				} else {
					if (normalizedCounter[s] >= largeLimit)
						DTableH.fastMode = 0;
					symbolNext[s] = normalizedCounter[s];
				}
			}
		}
		memcpy(dt, &DTableH, sizeof(DTableH));
	}

	 
	{
                /*
                 * A normalised count of -1 has already reserved a terminal
                 * table slot for a rare symbol. Spread the positive counts
                 * across the remaining states using the fixed step. Returning
                 * to zero checks that the distribution completed a cycle.
                 */
		U32 const tableMask = tableSize - 1;
		U32 const step = FSE_TABLESTEP(tableSize);
		U32 s, position = 0;
		for (s = 0; s < maxSV1; s++) {
			int i;
			for (i = 0; i < normalizedCounter[s]; i++) {
				tableDecode[position].symbol = (FSE_FUNCTION_TYPE)s;
				position = (position + step) & tableMask;
				while (position > highThreshold)
					position = (position + step) & tableMask;  
			}
		}
		if (position != 0)
			return ERROR(GENERIC);  
	}

	 
	{
		U32 u;
		for (u = 0; u < tableSize; u++) {
			FSE_FUNCTION_TYPE const symbol = (FSE_FUNCTION_TYPE)(tableDecode[u].symbol);
			U16 nextState = symbolNext[symbol]++;
			tableDecode[u].nbBits = (BYTE)(tableLog - BIT_highbit32((U32)nextState));
			tableDecode[u].newState = (U16)((nextState << tableDecode[u].nbBits) - tableSize);
		}
	}

	return 0;
}

 


size_t FSE_buildDTable_rle(FSE_DTable *dt, BYTE symbolValue)
{
	void *ptr = dt;
	FSE_DTableHeader *const DTableH = (FSE_DTableHeader *)ptr;
	void *dPtr = dt + 1;
	FSE_decode_t *const cell = (FSE_decode_t *)dPtr;

	DTableH->tableLog = 0;
	DTableH->fastMode = 0;

	cell->newState = 0;
	cell->symbol = symbolValue;
	cell->nbBits = 0;

	return 0;
}

FORCE_INLINE size_t FSE_decompress_usingDTable_generic(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize, const FSE_DTable *dt,
						       const unsigned fast)
{
	BYTE *const ostart = (BYTE *)dst;
	BYTE *op = ostart;
	BYTE *const omax = op + maxDstSize;
	BYTE *const olimit = omax - 3;

	BIT_DStream_t bitD;
	FSE_DState_t state1;
	FSE_DState_t state2;

	 
	CHECK_F(BIT_initDStream(&bitD, cSrc, cSrcSize));

	FSE_initDState(&state1, &bitD, dt);
	FSE_initDState(&state2, &bitD, dt);

#define FSE_GETSYMBOL(statePtr) fast ? FSE_decodeSymbolFast(statePtr, &bitD) : FSE_decodeSymbol(statePtr, &bitD)

	 
	for (; (BIT_reloadDStream(&bitD) == BIT_DStream_unfinished) & (op < olimit); op += 4) {
		op[0] = FSE_GETSYMBOL(&state1);

		if (FSE_MAX_TABLELOG * 2 + 7 > sizeof(bitD.bitContainer) * 8)  
			BIT_reloadDStream(&bitD);

		op[1] = FSE_GETSYMBOL(&state2);

		if (FSE_MAX_TABLELOG * 4 + 7 > sizeof(bitD.bitContainer) * 8)  
		{
			if (BIT_reloadDStream(&bitD) > BIT_DStream_unfinished) {
				op += 2;
				break;
			}
		}

		op[2] = FSE_GETSYMBOL(&state1);

		if (FSE_MAX_TABLELOG * 2 + 7 > sizeof(bitD.bitContainer) * 8)  
			BIT_reloadDStream(&bitD);

		op[3] = FSE_GETSYMBOL(&state2);
	}

	 
	 
	while (1) {
		if (op > (omax - 2))
			return ERROR(dstSize_tooSmall);
		*op++ = FSE_GETSYMBOL(&state1);
		if (BIT_reloadDStream(&bitD) == BIT_DStream_overflow) {
			*op++ = FSE_GETSYMBOL(&state2);
			break;
		}

		if (op > (omax - 2))
			return ERROR(dstSize_tooSmall);
		*op++ = FSE_GETSYMBOL(&state2);
		if (BIT_reloadDStream(&bitD) == BIT_DStream_overflow) {
			*op++ = FSE_GETSYMBOL(&state1);
			break;
		}
	}

	return op - ostart;
}

size_t FSE_decompress_usingDTable(void *dst, size_t originalSize, const void *cSrc, size_t cSrcSize, const FSE_DTable *dt)
{
	const void *ptr = dt;
	const FSE_DTableHeader *DTableH = (const FSE_DTableHeader *)ptr;
	const U32 fastMode = DTableH->fastMode;

	 
	if (fastMode)
		return FSE_decompress_usingDTable_generic(dst, originalSize, cSrc, cSrcSize, dt, 1);
	return FSE_decompress_usingDTable_generic(dst, originalSize, cSrc, cSrcSize, dt, 0);
}

/*
 * Uses a caller-supplied workspace and bounded output buffer. A malformed
 * bitstream must report failure rather than exceed the destination.
 */
size_t FSE_decompress_wksp(void *dst, size_t dstCapacity, const void *cSrc, size_t cSrcSize, unsigned maxLog, void *workspace, size_t workspaceSize)
{
	const BYTE *const istart = (const BYTE *)cSrc;
	const BYTE *ip = istart;
	unsigned tableLog;
	unsigned maxSymbolValue = FSE_MAX_SYMBOL_VALUE;
	size_t NCountLength;

	FSE_DTable *dt;
	short *counting;
	size_t spaceUsed32 = 0;

	FSE_STATIC_ASSERT(sizeof(FSE_DTable) == sizeof(U32));

	dt = (FSE_DTable *)((U32 *)workspace + spaceUsed32);
	spaceUsed32 += FSE_DTABLE_SIZE_U32(maxLog);
	counting = (short *)((U32 *)workspace + spaceUsed32);
	spaceUsed32 += UP_U32(sizeof(short) * (FSE_MAX_SYMBOL_VALUE + 1));

	if ((spaceUsed32 << 2) > workspaceSize)
		return ERROR(tableLog_tooLarge);
	workspace = (U32 *)workspace + spaceUsed32;
	workspaceSize -= (spaceUsed32 << 2);

	 
	NCountLength = FSE_readNCount(counting, &maxSymbolValue, &tableLog, istart, cSrcSize);
	if (FSE_isError(NCountLength))
		return NCountLength;
	 
	 
	if (tableLog > maxLog)
		return ERROR(tableLog_tooLarge);
	ip += NCountLength;
	cSrcSize -= NCountLength;

	CHECK_F(FSE_buildDTable_wksp(dt, counting, maxSymbolValue, tableLog, workspace, workspaceSize));

	return FSE_decompress_usingDTable(dst, dstCapacity, ip, cSrcSize, dt);  
}

 


size_t FSE_readNCount(short *normalizedCounter, unsigned *maxSVPtr, unsigned *tableLogPtr, const void *headerBuffer, size_t hbSize)
{
	const BYTE *const istart = (const BYTE *)headerBuffer;
	const BYTE *const iend = istart + hbSize;
	const BYTE *ip = istart;
	int nbBits;
	int remaining;
	int threshold;
	U32 bitStream;
	int bitCount;
	unsigned charnum = 0;
	int previous0 = 0;

	if (hbSize < 4)
		return ERROR(srcSize_wrong);
	bitStream = ZSTD_readLE32(ip);
	nbBits = (bitStream & 0xF) + FSE_MIN_TABLELOG;  
	if (nbBits > FSE_TABLELOG_ABSOLUTE_MAX)
		return ERROR(tableLog_tooLarge);
	bitStream >>= 4;
	bitCount = 4;
	*tableLogPtr = nbBits;
	remaining = (1 << nbBits) + 1;
	threshold = 1 << nbBits;
	nbBits++;

	while ((remaining > 1) & (charnum <= *maxSVPtr)) {
		if (previous0) {
			unsigned n0 = charnum;
			while ((bitStream & 0xFFFF) == 0xFFFF) {
				n0 += 24;
				if (ip < iend - 5) {
					ip += 2;
					bitStream = ZSTD_readLE32(ip) >> bitCount;
				} else {
					bitStream >>= 16;
					bitCount += 16;
				}
			}
			while ((bitStream & 3) == 3) {
				n0 += 3;
				bitStream >>= 2;
				bitCount += 2;
			}
			n0 += bitStream & 3;
			bitCount += 2;
			if (n0 > *maxSVPtr)
				return ERROR(maxSymbolValue_tooSmall);
			while (charnum < n0)
				normalizedCounter[charnum++] = 0;
			if ((ip <= iend - 7) || (ip + (bitCount >> 3) <= iend - 4)) {
				ip += bitCount >> 3;
				bitCount &= 7;
				bitStream = ZSTD_readLE32(ip) >> bitCount;
			} else {
				bitStream >>= 2;
			}
		}
		{
			int const max = (2 * threshold - 1) - remaining;
			int count;

			if ((bitStream & (threshold - 1)) < (U32)max) {
				count = bitStream & (threshold - 1);
				bitCount += nbBits - 1;
			} else {
				count = bitStream & (2 * threshold - 1);
				if (count >= threshold)
					count -= max;
				bitCount += nbBits;
			}

			count--;				  
			remaining -= count < 0 ? -count : count;  
			normalizedCounter[charnum++] = (short)count;
			previous0 = !count;
			while (remaining < threshold) {
				nbBits--;
				threshold >>= 1;
			}

			if ((ip <= iend - 7) || (ip + (bitCount >> 3) <= iend - 4)) {
				ip += bitCount >> 3;
				bitCount &= 7;
			} else {
				bitCount -= (int)(8 * (iend - 4 - ip));
				ip = iend - 4;
			}
			bitStream = ZSTD_readLE32(ip) >> (bitCount & 31);
		}
	}  
	if (remaining != 1)
		return ERROR(corruption_detected);
	if (bitCount > 32)
		return ERROR(corruption_detected);
	*maxSVPtr = charnum - 1;

	ip += (bitCount + 7) >> 3;
	return ip - istart;
}
