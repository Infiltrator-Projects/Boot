/*
 * Bundled Zstandard frame and block decoder used by compressed filesystem
 * data. Frame headers, window sizes, literal/sequence tables and match offsets
 * are untrusted. A matching magic number does not establish available output
 * capacity or a supported dictionary; decoder errors must reach the caller.
 */

#ifndef ZSTD_MAXWINDOWSIZE_DEFAULT
#define ZSTD_MAXWINDOWSIZE_DEFAULT ((1 << ZSTD_WINDOWLOG_MAX) + 1)  
#endif

#include "fse.h"
#include "huf.h"
#include "mem.h"  
#include "zstd_internal.h"

#define ZSTD_PREFETCH(ptr) __builtin_prefetch(ptr, 0, 0)

static void ZSTD_copy4(void *dst, const void *src) { memcpy(dst, src, 4); }

typedef enum {
	ZSTDds_getFrameHeaderSize,
	ZSTDds_decodeFrameHeader,
	ZSTDds_decodeBlockHeader,
	ZSTDds_decompressBlock,
	ZSTDds_decompressLastBlock,
	ZSTDds_checkChecksum,
	ZSTDds_decodeSkippableHeader,
	ZSTDds_skipFrame
} ZSTD_dStage;

typedef enum {
    ZSTDnit_frameHeader,
    ZSTDnit_blockHeader,
    ZSTDnit_block,
    ZSTDnit_lastBlock,
    ZSTDnit_checksum,
    ZSTDnit_skippableFrame
} ZSTD_nextInputType_e;

typedef struct {
	FSE_DTable LLTable[FSE_DTABLE_SIZE_U32(LLFSELog)];
	FSE_DTable OFTable[FSE_DTABLE_SIZE_U32(OffFSELog)];
	FSE_DTable MLTable[FSE_DTABLE_SIZE_U32(MLFSELog)];
	HUF_DTable hufTable[HUF_DTABLE_SIZE(HufLog)];  
	U64 workspace[HUF_DECOMPRESS_WORKSPACE_SIZE_U32 / 2];
	U32 rep[ZSTD_REP_NUM];
} ZSTD_entropyTables_t;

struct ZSTD_DCtx_s {
	const FSE_DTable *LLTptr;
	const FSE_DTable *MLTptr;
	const FSE_DTable *OFTptr;
	const HUF_DTable *HUFptr;
	ZSTD_entropyTables_t entropy;
	const void *previousDstEnd;  
	const void *base;	    
	const void *vBase;	   
	const void *dictEnd;	 
	size_t expected;
	ZSTD_frameParams fParams;
	blockType_e bType;  
	ZSTD_dStage stage;
	U32 litEntropy;
	U32 fseEntropy;
	struct xxh64_state xxhState;
	size_t headerSize;
	U32 dictID;
	const BYTE *litPtr;
	size_t litSize;
	size_t rleSize;
	BYTE litBuffer[ZSTD_BLOCKSIZE_ABSOLUTEMAX + WILDCOPY_OVERLENGTH];
	BYTE headerBuffer[ZSTD_FRAMEHEADERSIZE_MAX];
};  

typedef struct ZSTD_DCtx_s ZSTD_DCtx;

size_t ZSTD_decompressBegin(ZSTD_DCtx *dctx)
{
	dctx->expected = ZSTD_frameHeaderSize_prefix;
	dctx->stage = ZSTDds_getFrameHeaderSize;
	dctx->previousDstEnd = NULL;
	dctx->base = NULL;
	dctx->vBase = NULL;
	dctx->dictEnd = NULL;
	dctx->entropy.hufTable[0] = (HUF_DTable)((HufLog)*0x1000001);  
	dctx->litEntropy = dctx->fseEntropy = 0;
	dctx->dictID = 0;
	ZSTD_STATIC_ASSERT(sizeof(dctx->entropy.rep) == sizeof(repStartValue));
	memcpy(dctx->entropy.rep, repStartValue, sizeof(repStartValue));  
	dctx->LLTptr = dctx->entropy.LLTable;
	dctx->MLTptr = dctx->entropy.MLTable;
	dctx->OFTptr = dctx->entropy.OFTable;
	dctx->HUFptr = dctx->entropy.hufTable;
	return 0;
}

static size_t ZSTD_frameHeaderSize(const void *src, size_t srcSize)
{
	if (srcSize < ZSTD_frameHeaderSize_prefix)
		return ERROR(srcSize_wrong);
	{
		BYTE const fhd = ((const BYTE *)src)[4];
		U32 const dictID = fhd & 3;
		U32 const singleSegment = (fhd >> 5) & 1;
		U32 const fcsId = fhd >> 6;
		return ZSTD_frameHeaderSize_prefix + !singleSegment + ZSTD_did_fieldSize[dictID] + ZSTD_fcs_fieldSize[fcsId] + (singleSegment && !fcsId);
	}
}

/*
 * Parses frame metadata before allocation and decode. Declared window and
 * content sizes must be checked against caller/environment memory limits.
 */
size_t ZSTD_getFrameParams(ZSTD_frameParams *fparamsPtr, const void *src, size_t srcSize)
{
	const BYTE *ip = (const BYTE *)src;

	if (srcSize < ZSTD_frameHeaderSize_prefix)
		return ZSTD_frameHeaderSize_prefix;
	if (ZSTD_readLE32(src) != ZSTD_MAGICNUMBER) {
		if ((ZSTD_readLE32(src) & 0xFFFFFFF0U) == ZSTD_MAGIC_SKIPPABLE_START) {
			if (srcSize < ZSTD_skippableHeaderSize)
				return ZSTD_skippableHeaderSize;  
			memset(fparamsPtr, 0, sizeof(*fparamsPtr));
			fparamsPtr->frameContentSize = ZSTD_readLE32((const char *)src + 4);
			fparamsPtr->windowSize = 0;  
			return 0;
		}
		return ERROR(prefix_unknown);
	}

	{
		size_t const fhsize = ZSTD_frameHeaderSize(src, srcSize);
		if (srcSize < fhsize)
			return fhsize;
	}

	{
		BYTE const fhdByte = ip[4];
		size_t pos = 5;
		U32 const dictIDSizeCode = fhdByte & 3;
		U32 const checksumFlag = (fhdByte >> 2) & 1;
		U32 const singleSegment = (fhdByte >> 5) & 1;
		U32 const fcsID = fhdByte >> 6;
		U32 const windowSizeMax = 1U << ZSTD_WINDOWLOG_MAX;
		U32 windowSize = 0;
		U32 dictID = 0;
		U64 frameContentSize = 0;
		if ((fhdByte & 0x08) != 0)
			return ERROR(frameParameter_unsupported);  
		if (!singleSegment) {
			BYTE const wlByte = ip[pos++];
			U32 const windowLog = (wlByte >> 3) + ZSTD_WINDOWLOG_ABSOLUTEMIN;
			if (windowLog > ZSTD_WINDOWLOG_MAX)
				return ERROR(frameParameter_windowTooLarge);  
			windowSize = (1U << windowLog);
			windowSize += (windowSize >> 3) * (wlByte & 7);
		}

		switch (dictIDSizeCode) {
		default:  
		case 0: break;
		case 1:
			dictID = ip[pos];
			pos++;
			break;
		case 2:
			dictID = ZSTD_readLE16(ip + pos);
			pos += 2;
			break;
		case 3:
			dictID = ZSTD_readLE32(ip + pos);
			pos += 4;
			break;
		}
		switch (fcsID) {
		default:  
		case 0:
			if (singleSegment)
				frameContentSize = ip[pos];
			break;
		case 1: frameContentSize = ZSTD_readLE16(ip + pos) + 256; break;
		case 2: frameContentSize = ZSTD_readLE32(ip + pos); break;
		case 3: frameContentSize = ZSTD_readLE64(ip + pos); break;
		}
		if (!windowSize)
			windowSize = (U32)frameContentSize;
		if (windowSize > windowSizeMax)
			return ERROR(frameParameter_windowTooLarge);
		fparamsPtr->frameContentSize = frameContentSize;
		fparamsPtr->windowSize = windowSize;
		fparamsPtr->dictID = dictID;
		fparamsPtr->checksumFlag = checksumFlag;
	}
	return 0;
}

static size_t ZSTD_decodeFrameHeader(ZSTD_DCtx *dctx, const void *src, size_t headerSize)
{
	size_t const result = ZSTD_getFrameParams(&(dctx->fParams), src, headerSize);
	if (ZSTD_isError(result))
		return result;  
	if (result > 0)
		return ERROR(srcSize_wrong);  
	if (dctx->fParams.dictID && (dctx->dictID != dctx->fParams.dictID))
		return ERROR(dictionary_wrong);
	if (dctx->fParams.checksumFlag)
		xxh64_reset(&dctx->xxhState, 0);
	return 0;
}

typedef struct {
	blockType_e blockType;
	U32 lastBlock;
	U32 origSize;
} blockProperties_t;

/**
 * Unpack a three-byte little-endian header. Bit zero terminates the frame, bits
 * one and two select the block type, and the remaining bits carry its size. RLE
 * uses one stored payload byte irrespective of its expanded byte count, which is
 * retained in origSize.
 */
/*
 * Classify a Zstandard block header and its declared compressed size. RLE
 * has one compressed value and a separate regenerated length, unlike raw or
 * compressed blocks. The caller must compare header-derived sizes with
 * remaining input before advancing; parsing does not prove filesystem
 * metadata authenticity.
 */
size_t ZSTD_getcBlockSize(const void *src, size_t srcSize, blockProperties_t *bpPtr)
{
	if (srcSize < ZSTD_blockHeaderSize)
		return ERROR(srcSize_wrong);
	{
		U32 const cBlockHeader = ZSTD_readLE24(src);
		U32 const cSize = cBlockHeader >> 3;
		bpPtr->lastBlock = cBlockHeader & 1;
		bpPtr->blockType = (blockType_e)((cBlockHeader >> 1) & 3);
		bpPtr->origSize = cSize;  
		if (bpPtr->blockType == bt_rle)
			return 1;
		if (bpPtr->blockType == bt_reserved)
			return ERROR(corruption_detected);
		return cSize;
	}
}

static size_t ZSTD_copyRawBlock(void *dst, size_t dstCapacity, const void *src, size_t srcSize)
{
	if (srcSize > dstCapacity)
		return ERROR(dstSize_tooSmall);
	memcpy(dst, src, srcSize);
	return srcSize;
}

static size_t ZSTD_setRleBlock(void *dst, size_t dstCapacity, const void *src, size_t srcSize, size_t regenSize)
{
	if (srcSize != 1)
		return ERROR(srcSize_wrong);
	if (regenSize > dstCapacity)
		return ERROR(dstSize_tooSmall);
	memset(dst, *(const BYTE *)src, regenSize);
	return regenSize;
}

/*
 * Decodes the literal section subject to source bytes and destination bounds;
 * corrupt entropy metadata must not become uninitialised output.
 */
size_t ZSTD_decodeLiteralsBlock(ZSTD_DCtx *dctx, const void *src, size_t srcSize)  
{
	if (srcSize < MIN_CBLOCK_SIZE)
		return ERROR(corruption_detected);

	{
		const BYTE *const istart = (const BYTE *)src;
		symbolEncodingType_e const litEncType = (symbolEncodingType_e)(istart[0] & 3);

		switch (litEncType) {
		case set_repeat:
			if (dctx->litEntropy == 0)
				return ERROR(dictionary_corrupted);

		case set_compressed:
			if (srcSize < 5)
				return ERROR(corruption_detected);  
			{
				size_t lhSize, litSize, litCSize;
				U32 singleStream = 0;
				U32 const lhlCode = (istart[0] >> 2) & 3;
				U32 const lhc = ZSTD_readLE32(istart);
				switch (lhlCode) {
				case 0:
				case 1:
				default:  

					singleStream = !lhlCode;
					lhSize = 3;
					litSize = (lhc >> 4) & 0x3FF;
					litCSize = (lhc >> 14) & 0x3FF;
					break;
				case 2:

					lhSize = 4;
					litSize = (lhc >> 4) & 0x3FFF;
					litCSize = lhc >> 18;
					break;
				case 3:

					lhSize = 5;
					litSize = (lhc >> 4) & 0x3FFFF;
					litCSize = (lhc >> 22) + (istart[4] << 10);
					break;
				}
				if (litSize > ZSTD_BLOCKSIZE_ABSOLUTEMAX)
					return ERROR(corruption_detected);
				if (litCSize + lhSize > srcSize)
					return ERROR(corruption_detected);

				if (HUF_isError(
					(litEncType == set_repeat)
					    ? (singleStream ? HUF_decompress1X_usingDTable(dctx->litBuffer, litSize, istart + lhSize, litCSize, dctx->HUFptr)
							    : HUF_decompress4X_usingDTable(dctx->litBuffer, litSize, istart + lhSize, litCSize, dctx->HUFptr))
					    : (singleStream
						   ? HUF_decompress1X2_DCtx_wksp(dctx->entropy.hufTable, dctx->litBuffer, litSize, istart + lhSize, litCSize,
										 dctx->entropy.workspace, sizeof(dctx->entropy.workspace))
						   : HUF_decompress4X_hufOnly_wksp(dctx->entropy.hufTable, dctx->litBuffer, litSize, istart + lhSize, litCSize,
										   dctx->entropy.workspace, sizeof(dctx->entropy.workspace)))))
					return ERROR(corruption_detected);

				dctx->litPtr = dctx->litBuffer;
				dctx->litSize = litSize;
				dctx->litEntropy = 1;
				if (litEncType == set_compressed)
					dctx->HUFptr = dctx->entropy.hufTable;
				memset(dctx->litBuffer + dctx->litSize, 0, WILDCOPY_OVERLENGTH);
				return litCSize + lhSize;
			}

		case set_basic: {
			size_t litSize, lhSize;
			U32 const lhlCode = ((istart[0]) >> 2) & 3;
			switch (lhlCode) {
			case 0:
			case 2:
			default:  
				lhSize = 1;
				litSize = istart[0] >> 3;
				break;
			case 1:
				lhSize = 2;
				litSize = ZSTD_readLE16(istart) >> 4;
				break;
			case 3:
				lhSize = 3;
				litSize = ZSTD_readLE24(istart) >> 4;
				break;
			}

			if (lhSize + litSize + WILDCOPY_OVERLENGTH > srcSize) {  
				if (litSize + lhSize > srcSize)
					return ERROR(corruption_detected);
				memcpy(dctx->litBuffer, istart + lhSize, litSize);
				dctx->litPtr = dctx->litBuffer;
				dctx->litSize = litSize;
				memset(dctx->litBuffer + dctx->litSize, 0, WILDCOPY_OVERLENGTH);
				return lhSize + litSize;
			}

			dctx->litPtr = istart + lhSize;
			dctx->litSize = litSize;
			return lhSize + litSize;
		}

		case set_rle: {
			U32 const lhlCode = ((istart[0]) >> 2) & 3;
			size_t litSize, lhSize;
			switch (lhlCode) {
			case 0:
			case 2:
			default:  
				lhSize = 1;
				litSize = istart[0] >> 3;
				break;
			case 1:
				lhSize = 2;
				litSize = ZSTD_readLE16(istart) >> 4;
				break;
			case 3:
				lhSize = 3;
				litSize = ZSTD_readLE24(istart) >> 4;
				if (srcSize < 4)
					return ERROR(corruption_detected);  
				break;
			}
			if (litSize > ZSTD_BLOCKSIZE_ABSOLUTEMAX)
				return ERROR(corruption_detected);
			memset(dctx->litBuffer, istart[lhSize], litSize + WILDCOPY_OVERLENGTH);
			dctx->litPtr = dctx->litBuffer;
			dctx->litSize = litSize;
			return lhSize + 1;
		}
		default:
			return ERROR(corruption_detected);  
		}
	}
}

typedef union {
	FSE_decode_t realData;
	U32 alignedBy4;
} FSE_decode_t4;

static const FSE_decode_t4 LL_defaultDTable[(1 << LL_DEFAULTNORMLOG) + 1] = {
    {{LL_DEFAULTNORMLOG, 1, 1}},  
    {{0, 0, 4}},		  
    {{16, 0, 4}},
    {{32, 1, 5}},
    {{0, 3, 5}},
    {{0, 4, 5}},
    {{0, 6, 5}},
    {{0, 7, 5}},
    {{0, 9, 5}},
    {{0, 10, 5}},
    {{0, 12, 5}},
    {{0, 14, 6}},
    {{0, 16, 5}},
    {{0, 18, 5}},
    {{0, 19, 5}},
    {{0, 21, 5}},
    {{0, 22, 5}},
    {{0, 24, 5}},
    {{32, 25, 5}},
    {{0, 26, 5}},
    {{0, 27, 6}},
    {{0, 29, 6}},
    {{0, 31, 6}},
    {{32, 0, 4}},
    {{0, 1, 4}},
    {{0, 2, 5}},
    {{32, 4, 5}},
    {{0, 5, 5}},
    {{32, 7, 5}},
    {{0, 8, 5}},
    {{32, 10, 5}},
    {{0, 11, 5}},
    {{0, 13, 6}},
    {{32, 16, 5}},
    {{0, 17, 5}},
    {{32, 19, 5}},
    {{0, 20, 5}},
    {{32, 22, 5}},
    {{0, 23, 5}},
    {{0, 25, 4}},
    {{16, 25, 4}},
    {{32, 26, 5}},
    {{0, 28, 6}},
    {{0, 30, 6}},
    {{48, 0, 4}},
    {{16, 1, 4}},
    {{32, 2, 5}},
    {{32, 3, 5}},
    {{32, 5, 5}},
    {{32, 6, 5}},
    {{32, 8, 5}},
    {{32, 9, 5}},
    {{32, 11, 5}},
    {{32, 12, 5}},
    {{0, 15, 6}},
    {{32, 17, 5}},
    {{32, 18, 5}},
    {{32, 20, 5}},
    {{32, 21, 5}},
    {{32, 23, 5}},
    {{32, 24, 5}},
    {{0, 35, 6}},
    {{0, 34, 6}},
    {{0, 33, 6}},
    {{0, 32, 6}},
};  

static const FSE_decode_t4 ML_defaultDTable[(1 << ML_DEFAULTNORMLOG) + 1] = {
    {{ML_DEFAULTNORMLOG, 1, 1}},  
    {{0, 0, 6}},		  
    {{0, 1, 4}},
    {{32, 2, 5}},
    {{0, 3, 5}},
    {{0, 5, 5}},
    {{0, 6, 5}},
    {{0, 8, 5}},
    {{0, 10, 6}},
    {{0, 13, 6}},
    {{0, 16, 6}},
    {{0, 19, 6}},
    {{0, 22, 6}},
    {{0, 25, 6}},
    {{0, 28, 6}},
    {{0, 31, 6}},
    {{0, 33, 6}},
    {{0, 35, 6}},
    {{0, 37, 6}},
    {{0, 39, 6}},
    {{0, 41, 6}},
    {{0, 43, 6}},
    {{0, 45, 6}},
    {{16, 1, 4}},
    {{0, 2, 4}},
    {{32, 3, 5}},
    {{0, 4, 5}},
    {{32, 6, 5}},
    {{0, 7, 5}},
    {{0, 9, 6}},
    {{0, 12, 6}},
    {{0, 15, 6}},
    {{0, 18, 6}},
    {{0, 21, 6}},
    {{0, 24, 6}},
    {{0, 27, 6}},
    {{0, 30, 6}},
    {{0, 32, 6}},
    {{0, 34, 6}},
    {{0, 36, 6}},
    {{0, 38, 6}},
    {{0, 40, 6}},
    {{0, 42, 6}},
    {{0, 44, 6}},
    {{32, 1, 4}},
    {{48, 1, 4}},
    {{16, 2, 4}},
    {{32, 4, 5}},
    {{32, 5, 5}},
    {{32, 7, 5}},
    {{32, 8, 5}},
    {{0, 11, 6}},
    {{0, 14, 6}},
    {{0, 17, 6}},
    {{0, 20, 6}},
    {{0, 23, 6}},
    {{0, 26, 6}},
    {{0, 29, 6}},
    {{0, 52, 6}},
    {{0, 51, 6}},
    {{0, 50, 6}},
    {{0, 49, 6}},
    {{0, 48, 6}},
    {{0, 47, 6}},
    {{0, 46, 6}},
};  

static const FSE_decode_t4 OF_defaultDTable[(1 << OF_DEFAULTNORMLOG) + 1] = {
    {{OF_DEFAULTNORMLOG, 1, 1}},  
    {{0, 0, 5}},		  
    {{0, 6, 4}},
    {{0, 9, 5}},
    {{0, 15, 5}},
    {{0, 21, 5}},
    {{0, 3, 5}},
    {{0, 7, 4}},
    {{0, 12, 5}},
    {{0, 18, 5}},
    {{0, 23, 5}},
    {{0, 5, 5}},
    {{0, 8, 4}},
    {{0, 14, 5}},
    {{0, 20, 5}},
    {{0, 2, 5}},
    {{16, 7, 4}},
    {{0, 11, 5}},
    {{0, 17, 5}},
    {{0, 22, 5}},
    {{0, 4, 5}},
    {{16, 8, 4}},
    {{0, 13, 5}},
    {{0, 19, 5}},
    {{0, 1, 5}},
    {{16, 6, 4}},
    {{0, 10, 5}},
    {{0, 16, 5}},
    {{0, 28, 5}},
    {{0, 27, 5}},
    {{0, 26, 5}},
    {{0, 25, 5}},
    {{0, 24, 5}},
};  
/*
 * The sequence header chooses a table separately for literal lengths, match
 * lengths and offsets. Repeating a table requires the corresponding entropy
 * state from an earlier block or dictionary. A freshly reset decoder cannot
 * infer those symbols from a repeat header alone.
 */
static size_t ZSTD_buildSeqTable(FSE_DTable *DTableSpace, const FSE_DTable **DTablePtr, symbolEncodingType_e type, U32 max, U32 maxLog, const void *src,
				 size_t srcSize, const FSE_decode_t4 *defaultTable, U32 flagRepeatTable, void *workspace, size_t workspaceSize)
{
	const void *const tmpPtr = defaultTable;  
	switch (type) {
	case set_rle:
		if (!srcSize)
			return ERROR(srcSize_wrong);
		if ((*(const BYTE *)src) > max)
			return ERROR(corruption_detected);
		FSE_buildDTable_rle(DTableSpace, *(const BYTE *)src);
		*DTablePtr = DTableSpace;
		return 1;
	case set_basic: *DTablePtr = (const FSE_DTable *)tmpPtr; return 0;
	case set_repeat:
		if (!flagRepeatTable)
			return ERROR(corruption_detected);
		return 0;
	default:  
	case set_compressed: {
		U32 tableLog;
		S16 *norm = (S16 *)workspace;
		size_t const spaceUsed32 = UP_U32(sizeof(S16) * (MaxSeq + 1));

		if ((spaceUsed32 << 2) > workspaceSize)
			return ERROR(GENERIC);
		workspace = (U32 *)workspace + spaceUsed32;
		workspaceSize -= (spaceUsed32 << 2);
		{
			size_t const headerSize = FSE_readNCount(norm, &max, &tableLog, src, srcSize);
			if (FSE_isError(headerSize))
				return ERROR(corruption_detected);
			if (tableLog > maxLog)
				return ERROR(corruption_detected);
			FSE_buildDTable_wksp(DTableSpace, norm, max, tableLog, workspace, workspaceSize);
			*DTablePtr = DTableSpace;
			return headerSize;
		}
	}
	}
}

size_t ZSTD_decodeSeqHeaders(ZSTD_DCtx *dctx, int *nbSeqPtr, const void *src, size_t srcSize)
{
	const BYTE *const istart = (const BYTE *const)src;
	const BYTE *const iend = istart + srcSize;
	const BYTE *ip = istart;

	if (srcSize < MIN_SEQUENCES_SIZE)
		return ERROR(srcSize_wrong);

	{
		int nbSeq = *ip++;
		if (!nbSeq) {
			*nbSeqPtr = 0;
			return 1;
		}
		if (nbSeq > 0x7F) {
			if (nbSeq == 0xFF) {
				if (ip + 2 > iend)
					return ERROR(srcSize_wrong);
				nbSeq = ZSTD_readLE16(ip) + LONGNBSEQ, ip += 2;
			} else {
				if (ip >= iend)
					return ERROR(srcSize_wrong);
				nbSeq = ((nbSeq - 0x80) << 8) + *ip++;
			}
		}
		*nbSeqPtr = nbSeq;
	}

	if (ip + 4 > iend)
		return ERROR(srcSize_wrong);  
	{
		symbolEncodingType_e const LLtype = (symbolEncodingType_e)(*ip >> 6);
		symbolEncodingType_e const OFtype = (symbolEncodingType_e)((*ip >> 4) & 3);
		symbolEncodingType_e const MLtype = (symbolEncodingType_e)((*ip >> 2) & 3);
		ip++;

		{
			size_t const llhSize = ZSTD_buildSeqTable(dctx->entropy.LLTable, &dctx->LLTptr, LLtype, MaxLL, LLFSELog, ip, iend - ip,
								  LL_defaultDTable, dctx->fseEntropy, dctx->entropy.workspace, sizeof(dctx->entropy.workspace));
			if (ZSTD_isError(llhSize))
				return ERROR(corruption_detected);
			ip += llhSize;
		}
		{
			size_t const ofhSize = ZSTD_buildSeqTable(dctx->entropy.OFTable, &dctx->OFTptr, OFtype, MaxOff, OffFSELog, ip, iend - ip,
								  OF_defaultDTable, dctx->fseEntropy, dctx->entropy.workspace, sizeof(dctx->entropy.workspace));
			if (ZSTD_isError(ofhSize))
				return ERROR(corruption_detected);
			ip += ofhSize;
		}
		{
			size_t const mlhSize = ZSTD_buildSeqTable(dctx->entropy.MLTable, &dctx->MLTptr, MLtype, MaxML, MLFSELog, ip, iend - ip,
								  ML_defaultDTable, dctx->fseEntropy, dctx->entropy.workspace, sizeof(dctx->entropy.workspace));
			if (ZSTD_isError(mlhSize))
				return ERROR(corruption_detected);
			ip += mlhSize;
		}
	}

	return ip - istart;
}

typedef struct {
	size_t litLength;
	size_t matchLength;
	size_t offset;
	const BYTE *match;
} seq_t;

typedef struct {
	BIT_DStream_t DStream;
	FSE_DState_t stateLL;
	FSE_DState_t stateOffb;
	FSE_DState_t stateML;
	size_t prevOffset[ZSTD_REP_NUM];
	const BYTE *base;
	size_t pos;
	uPtrDiff gotoDict;
} seqState_t;

FORCE_NOINLINE
size_t ZSTD_execSequenceLast7(BYTE *op, BYTE *const oend, seq_t sequence, const BYTE **litPtr, const BYTE *const litLimit, const BYTE *const base,
			      const BYTE *const vBase, const BYTE *const dictEnd)
{
	BYTE *const oLitEnd = op + sequence.litLength;
	size_t const sequenceLength = sequence.litLength + sequence.matchLength;
	BYTE *const oMatchEnd = op + sequenceLength;  
	BYTE *const oend_w = oend - WILDCOPY_OVERLENGTH;
	const BYTE *const iLitEnd = *litPtr + sequence.litLength;
	const BYTE *match = oLitEnd - sequence.offset;

	if (oMatchEnd > oend)
		return ERROR(dstSize_tooSmall);  
	if (iLitEnd > litLimit)
		return ERROR(corruption_detected);  
	if (oLitEnd <= oend_w)
		return ERROR(GENERIC);  

	if (op < oend_w) {
		ZSTD_wildcopy(op, *litPtr, oend_w - op);
		*litPtr += oend_w - op;
		op = oend_w;
	}
	while (op < oLitEnd)
		*op++ = *(*litPtr)++;

	if (sequence.offset > (size_t)(oLitEnd - base)) {

		if (sequence.offset > (size_t)(oLitEnd - vBase))
			return ERROR(corruption_detected);
		match = dictEnd - (base - match);
		if (match + sequence.matchLength <= dictEnd) {
			sys_memmove(oLitEnd, match, sequence.matchLength);
			return sequenceLength;
		}

		{
			size_t const length1 = dictEnd - match;
			sys_memmove(oLitEnd, match, length1);
			op = oLitEnd + length1;
			sequence.matchLength -= length1;
			match = base;
		}
	}
	while (op < oMatchEnd)
		*op++ = *match++;
	return sequenceLength;
}

static seq_t ZSTD_decodeSequence(seqState_t *seqState)
{
	seq_t seq;

	U32 const llCode = FSE_peekSymbol(&seqState->stateLL);
	U32 const mlCode = FSE_peekSymbol(&seqState->stateML);
	U32 const ofCode = FSE_peekSymbol(&seqState->stateOffb);  

	U32 const llBits = LL_bits[llCode];
	U32 const mlBits = ML_bits[mlCode];
	U32 const ofBits = ofCode;
	U32 const totalBits = llBits + mlBits + ofBits;

	static const U32 LL_base[MaxLL + 1] = {0,  1,  2,  3,  4,  5,  6,  7,  8,    9,     10,    11,    12,    13,     14,     15,     16,     18,
					       20, 22, 24, 28, 32, 40, 48, 64, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000, 0x10000};

	static const U32 ML_base[MaxML + 1] = {3,  4,  5,  6,  7,  8,  9,  10,   11,    12,    13,    14,    15,     16,     17,     18,     19,     20,
					       21, 22, 23, 24, 25, 26, 27, 28,   29,    30,    31,    32,    33,     34,     35,     37,     39,     41,
					       43, 47, 51, 59, 67, 83, 99, 0x83, 0x103, 0x203, 0x403, 0x803, 0x1003, 0x2003, 0x4003, 0x8003, 0x10003};

	static const U32 OF_base[MaxOff + 1] = {0,       1,	1,	5,	0xD,      0x1D,      0x3D,      0x7D,      0xFD,     0x1FD,
						0x3FD,   0x7FD,    0xFFD,    0x1FFD,   0x3FFD,   0x7FFD,    0xFFFD,    0x1FFFD,   0x3FFFD,  0x7FFFD,
						0xFFFFD, 0x1FFFFD, 0x3FFFFD, 0x7FFFFD, 0xFFFFFD, 0x1FFFFFD, 0x3FFFFFD, 0x7FFFFFD, 0xFFFFFFD};

	{
		size_t offset;
		if (!ofCode)
			offset = 0;
		else {
			offset = OF_base[ofCode] + BIT_readBitsFast(&seqState->DStream, ofBits);  
			if (ZSTD_32bits())
				BIT_reloadDStream(&seqState->DStream);
		}

		if (ofCode <= 1) {
			offset += (llCode == 0);
			if (offset) {
				size_t temp = (offset == 3) ? seqState->prevOffset[0] - 1 : seqState->prevOffset[offset];
				temp += !temp;  
				if (offset != 1)
					seqState->prevOffset[2] = seqState->prevOffset[1];
				seqState->prevOffset[1] = seqState->prevOffset[0];
				seqState->prevOffset[0] = offset = temp;
			} else {
				offset = seqState->prevOffset[0];
			}
		} else {
			seqState->prevOffset[2] = seqState->prevOffset[1];
			seqState->prevOffset[1] = seqState->prevOffset[0];
			seqState->prevOffset[0] = offset;
		}
		seq.offset = offset;
	}

	seq.matchLength = ML_base[mlCode] + ((mlCode > 31) ? BIT_readBitsFast(&seqState->DStream, mlBits) : 0);  
	if (ZSTD_32bits() && (mlBits + llBits > 24))
		BIT_reloadDStream(&seqState->DStream);

	seq.litLength = LL_base[llCode] + ((llCode > 15) ? BIT_readBitsFast(&seqState->DStream, llBits) : 0);  
	if (ZSTD_32bits() || (totalBits > 64 - 7 - (LLFSELog + MLFSELog + OffFSELog)))
		BIT_reloadDStream(&seqState->DStream);

	FSE_updateState(&seqState->stateLL, &seqState->DStream);  
	FSE_updateState(&seqState->stateML, &seqState->DStream);  
	if (ZSTD_32bits())
		BIT_reloadDStream(&seqState->DStream);		    
	FSE_updateState(&seqState->stateOffb, &seqState->DStream);  

	seq.match = NULL;

	return seq;
}

FORCE_INLINE
size_t ZSTD_execSequence(BYTE *op, BYTE *const oend, seq_t sequence, const BYTE **litPtr, const BYTE *const litLimit, const BYTE *const base,
			 const BYTE *const vBase, const BYTE *const dictEnd)
{
	BYTE *const oLitEnd = op + sequence.litLength;
	size_t const sequenceLength = sequence.litLength + sequence.matchLength;
	BYTE *const oMatchEnd = op + sequenceLength;  
	BYTE *const oend_w = oend - WILDCOPY_OVERLENGTH;
	const BYTE *const iLitEnd = *litPtr + sequence.litLength;
	const BYTE *match = oLitEnd - sequence.offset;

	if (oMatchEnd > oend)
		return ERROR(dstSize_tooSmall);  
	if (iLitEnd > litLimit)
		return ERROR(corruption_detected);  
	if (oLitEnd > oend_w)
		return ZSTD_execSequenceLast7(op, oend, sequence, litPtr, litLimit, base, vBase, dictEnd);

	ZSTD_copy8(op, *litPtr);
	if (sequence.litLength > 8)
		ZSTD_wildcopy(op + 8, (*litPtr) + 8,
			      sequence.litLength - 8);  
	op = oLitEnd;
	*litPtr = iLitEnd;  

	if (sequence.offset > (size_t)(oLitEnd - base)) {

		if (sequence.offset > (size_t)(oLitEnd - vBase))
			return ERROR(corruption_detected);
		match = dictEnd + (match - base);
		if (match + sequence.matchLength <= dictEnd) {
			sys_memmove(oLitEnd, match, sequence.matchLength);
			return sequenceLength;
		}

		{
			size_t const length1 = dictEnd - match;
			sys_memmove(oLitEnd, match, length1);
			op = oLitEnd + length1;
			sequence.matchLength -= length1;
			match = base;
			if (op > oend_w || sequence.matchLength < MINMATCH) {
				U32 i;
				for (i = 0; i < sequence.matchLength; ++i)
					op[i] = match[i];
				return sequenceLength;
			}
		}
	}

	if (sequence.offset < 8) {

		static const U32 dec32table[] = {0, 1, 2, 1, 4, 4, 4, 4};    
		static const int dec64table[] = {8, 8, 8, 7, 8, 9, 10, 11};  
		int const sub2 = dec64table[sequence.offset];
		op[0] = match[0];
		op[1] = match[1];
		op[2] = match[2];
		op[3] = match[3];
		match += dec32table[sequence.offset];
		ZSTD_copy4(op + 4, match);
		match -= sub2;
	} else {
		ZSTD_copy8(op, match);
	}
	op += 8;
	match += 8;

	if (oMatchEnd > oend - (16 - MINMATCH)) {
		if (op < oend_w) {
			ZSTD_wildcopy(op, match, oend_w - op);
			match += oend_w - op;
			op = oend_w;
		}
		while (op < oMatchEnd)
			*op++ = *match++;
	} else {
		ZSTD_wildcopy(op, match, (ptrdiff_t)sequence.matchLength - 8);  
	}
	return sequenceLength;
}

static size_t ZSTD_decompressSequences(ZSTD_DCtx *dctx, void *dst, size_t maxDstSize, const void *seqStart, size_t seqSize)
{
	const BYTE *ip = (const BYTE *)seqStart;
	const BYTE *const iend = ip + seqSize;
	BYTE *const ostart = (BYTE * const)dst;
	BYTE *const oend = ostart + maxDstSize;
	BYTE *op = ostart;
	const BYTE *litPtr = dctx->litPtr;
	const BYTE *const litEnd = litPtr + dctx->litSize;
	const BYTE *const base = (const BYTE *)(dctx->base);
	const BYTE *const vBase = (const BYTE *)(dctx->vBase);
	const BYTE *const dictEnd = (const BYTE *)(dctx->dictEnd);
	int nbSeq;

	{
		size_t const seqHSize = ZSTD_decodeSeqHeaders(dctx, &nbSeq, ip, seqSize);
		if (ZSTD_isError(seqHSize))
			return seqHSize;
		ip += seqHSize;
	}

	if (nbSeq) {
		seqState_t seqState;
		dctx->fseEntropy = 1;
		{
			U32 i;
			for (i = 0; i < ZSTD_REP_NUM; i++)
				seqState.prevOffset[i] = dctx->entropy.rep[i];
		}
		CHECK_E(BIT_initDStream(&seqState.DStream, ip, iend - ip), corruption_detected);
		FSE_initDState(&seqState.stateLL, &seqState.DStream, dctx->LLTptr);
		FSE_initDState(&seqState.stateOffb, &seqState.DStream, dctx->OFTptr);
		FSE_initDState(&seqState.stateML, &seqState.DStream, dctx->MLTptr);

		for (; (BIT_reloadDStream(&(seqState.DStream)) <= BIT_DStream_completed) && nbSeq;) {
			nbSeq--;
			{
				seq_t const sequence = ZSTD_decodeSequence(&seqState);
				size_t const oneSeqSize = ZSTD_execSequence(op, oend, sequence, &litPtr, litEnd, base, vBase, dictEnd);
				if (ZSTD_isError(oneSeqSize))
					return oneSeqSize;
				op += oneSeqSize;
			}
		}

		if (nbSeq)
			return ERROR(corruption_detected);

		{
			U32 i;
			for (i = 0; i < ZSTD_REP_NUM; i++)
				dctx->entropy.rep[i] = (U32)(seqState.prevOffset[i]);
		}
	}

	{
		size_t const lastLLSize = litEnd - litPtr;
		if (lastLLSize > (size_t)(oend - op))
			return ERROR(dstSize_tooSmall);
		memcpy(op, litPtr, lastLLSize);
		op += lastLLSize;
	}

	return op - ostart;
}

FORCE_INLINE seq_t ZSTD_decodeSequenceLong_generic(seqState_t *seqState, int const longOffsets)
{
	seq_t seq;

	U32 const llCode = FSE_peekSymbol(&seqState->stateLL);
	U32 const mlCode = FSE_peekSymbol(&seqState->stateML);
	U32 const ofCode = FSE_peekSymbol(&seqState->stateOffb);  

	U32 const llBits = LL_bits[llCode];
	U32 const mlBits = ML_bits[mlCode];
	U32 const ofBits = ofCode;
	U32 const totalBits = llBits + mlBits + ofBits;

	static const U32 LL_base[MaxLL + 1] = {0,  1,  2,  3,  4,  5,  6,  7,  8,    9,     10,    11,    12,    13,     14,     15,     16,     18,
					       20, 22, 24, 28, 32, 40, 48, 64, 0x80, 0x100, 0x200, 0x400, 0x800, 0x1000, 0x2000, 0x4000, 0x8000, 0x10000};

	static const U32 ML_base[MaxML + 1] = {3,  4,  5,  6,  7,  8,  9,  10,   11,    12,    13,    14,    15,     16,     17,     18,     19,     20,
					       21, 22, 23, 24, 25, 26, 27, 28,   29,    30,    31,    32,    33,     34,     35,     37,     39,     41,
					       43, 47, 51, 59, 67, 83, 99, 0x83, 0x103, 0x203, 0x403, 0x803, 0x1003, 0x2003, 0x4003, 0x8003, 0x10003};

	static const U32 OF_base[MaxOff + 1] = {0,       1,	1,	5,	0xD,      0x1D,      0x3D,      0x7D,      0xFD,     0x1FD,
						0x3FD,   0x7FD,    0xFFD,    0x1FFD,   0x3FFD,   0x7FFD,    0xFFFD,    0x1FFFD,   0x3FFFD,  0x7FFFD,
						0xFFFFD, 0x1FFFFD, 0x3FFFFD, 0x7FFFFD, 0xFFFFFD, 0x1FFFFFD, 0x3FFFFFD, 0x7FFFFFD, 0xFFFFFFD};

	{
		size_t offset;
		if (!ofCode)
			offset = 0;
		else {
			if (longOffsets) {
				int const extraBits = ofBits - MIN(ofBits, STREAM_ACCUMULATOR_MIN);
				offset = OF_base[ofCode] + (BIT_readBitsFast(&seqState->DStream, ofBits - extraBits) << extraBits);
				if (ZSTD_32bits() || extraBits)
					BIT_reloadDStream(&seqState->DStream);
				if (extraBits)
					offset += BIT_readBitsFast(&seqState->DStream, extraBits);
			} else {
				offset = OF_base[ofCode] + BIT_readBitsFast(&seqState->DStream, ofBits);  
				if (ZSTD_32bits())
					BIT_reloadDStream(&seqState->DStream);
			}
		}

		if (ofCode <= 1) {
			offset += (llCode == 0);
			if (offset) {
				size_t temp = (offset == 3) ? seqState->prevOffset[0] - 1 : seqState->prevOffset[offset];
				temp += !temp;  
				if (offset != 1)
					seqState->prevOffset[2] = seqState->prevOffset[1];
				seqState->prevOffset[1] = seqState->prevOffset[0];
				seqState->prevOffset[0] = offset = temp;
			} else {
				offset = seqState->prevOffset[0];
			}
		} else {
			seqState->prevOffset[2] = seqState->prevOffset[1];
			seqState->prevOffset[1] = seqState->prevOffset[0];
			seqState->prevOffset[0] = offset;
		}
		seq.offset = offset;
	}

	seq.matchLength = ML_base[mlCode] + ((mlCode > 31) ? BIT_readBitsFast(&seqState->DStream, mlBits) : 0);  
	if (ZSTD_32bits() && (mlBits + llBits > 24))
		BIT_reloadDStream(&seqState->DStream);

	seq.litLength = LL_base[llCode] + ((llCode > 15) ? BIT_readBitsFast(&seqState->DStream, llBits) : 0);  
	if (ZSTD_32bits() || (totalBits > 64 - 7 - (LLFSELog + MLFSELog + OffFSELog)))
		BIT_reloadDStream(&seqState->DStream);

	{
		size_t const pos = seqState->pos + seq.litLength;
		seq.match = seqState->base + pos - seq.offset;  
		if (seq.offset > pos)
			seq.match += seqState->gotoDict;  
		seqState->pos = pos + seq.matchLength;
	}

	FSE_updateState(&seqState->stateLL, &seqState->DStream);  
	FSE_updateState(&seqState->stateML, &seqState->DStream);  
	if (ZSTD_32bits())
		BIT_reloadDStream(&seqState->DStream);		    
	FSE_updateState(&seqState->stateOffb, &seqState->DStream);  

	return seq;
}

static seq_t ZSTD_decodeSequenceLong(seqState_t *seqState, unsigned const windowSize)
{
	if (ZSTD_highbit32(windowSize) > STREAM_ACCUMULATOR_MIN) {
		return ZSTD_decodeSequenceLong_generic(seqState, 1);
	} else {
		return ZSTD_decodeSequenceLong_generic(seqState, 0);
	}
}

FORCE_INLINE
size_t ZSTD_execSequenceLong(BYTE *op, BYTE *const oend, seq_t sequence, const BYTE **litPtr, const BYTE *const litLimit, const BYTE *const base,
			     const BYTE *const vBase, const BYTE *const dictEnd)
{
	BYTE *const oLitEnd = op + sequence.litLength;
	size_t const sequenceLength = sequence.litLength + sequence.matchLength;
	BYTE *const oMatchEnd = op + sequenceLength;  
	BYTE *const oend_w = oend - WILDCOPY_OVERLENGTH;
	const BYTE *const iLitEnd = *litPtr + sequence.litLength;
	const BYTE *match = sequence.match;

	if (oMatchEnd > oend)
		return ERROR(dstSize_tooSmall);  
	if (iLitEnd > litLimit)
		return ERROR(corruption_detected);  
	if (oLitEnd > oend_w)
		return ZSTD_execSequenceLast7(op, oend, sequence, litPtr, litLimit, base, vBase, dictEnd);

	ZSTD_copy8(op, *litPtr);
	if (sequence.litLength > 8)
		ZSTD_wildcopy(op + 8, (*litPtr) + 8,
			      sequence.litLength - 8);  
	op = oLitEnd;
	*litPtr = iLitEnd;  

	if (sequence.offset > (size_t)(oLitEnd - base)) {

		if (sequence.offset > (size_t)(oLitEnd - vBase))
			return ERROR(corruption_detected);
		if (match + sequence.matchLength <= dictEnd) {
			sys_memmove(oLitEnd, match, sequence.matchLength);
			return sequenceLength;
		}

		{
			size_t const length1 = dictEnd - match;
			sys_memmove(oLitEnd, match, length1);
			op = oLitEnd + length1;
			sequence.matchLength -= length1;
			match = base;
			if (op > oend_w || sequence.matchLength < MINMATCH) {
				U32 i;
				for (i = 0; i < sequence.matchLength; ++i)
					op[i] = match[i];
				return sequenceLength;
			}
		}
	}

	if (sequence.offset < 8) {

		static const U32 dec32table[] = {0, 1, 2, 1, 4, 4, 4, 4};    
		static const int dec64table[] = {8, 8, 8, 7, 8, 9, 10, 11};  
		int const sub2 = dec64table[sequence.offset];
		op[0] = match[0];
		op[1] = match[1];
		op[2] = match[2];
		op[3] = match[3];
		match += dec32table[sequence.offset];
		ZSTD_copy4(op + 4, match);
		match -= sub2;
	} else {
		ZSTD_copy8(op, match);
	}
	op += 8;
	match += 8;

	if (oMatchEnd > oend - (16 - MINMATCH)) {
		if (op < oend_w) {
			ZSTD_wildcopy(op, match, oend_w - op);
			match += oend_w - op;
			op = oend_w;
		}
		while (op < oMatchEnd)
			*op++ = *match++;
	} else {
		ZSTD_wildcopy(op, match, (ptrdiff_t)sequence.matchLength - 8);  
	}
	return sequenceLength;
}

static size_t ZSTD_decompressSequencesLong(ZSTD_DCtx *dctx, void *dst, size_t maxDstSize, const void *seqStart, size_t seqSize)
{
	const BYTE *ip = (const BYTE *)seqStart;
	const BYTE *const iend = ip + seqSize;
	BYTE *const ostart = (BYTE * const)dst;
	BYTE *const oend = ostart + maxDstSize;
	BYTE *op = ostart;
	const BYTE *litPtr = dctx->litPtr;
	const BYTE *const litEnd = litPtr + dctx->litSize;
	const BYTE *const base = (const BYTE *)(dctx->base);
	const BYTE *const vBase = (const BYTE *)(dctx->vBase);
	const BYTE *const dictEnd = (const BYTE *)(dctx->dictEnd);
	unsigned const windowSize = dctx->fParams.windowSize;
	int nbSeq;

	{
		size_t const seqHSize = ZSTD_decodeSeqHeaders(dctx, &nbSeq, ip, seqSize);
		if (ZSTD_isError(seqHSize))
			return seqHSize;
		ip += seqHSize;
	}

	if (nbSeq) {
#define STORED_SEQS 4
#define STOSEQ_MASK (STORED_SEQS - 1)
#define ADVANCED_SEQS 4
		seq_t *sequences = (seq_t *)dctx->entropy.workspace;
		int const seqAdvance = MIN(nbSeq, ADVANCED_SEQS);
		seqState_t seqState;
		int seqNb;
		ZSTD_STATIC_ASSERT(sizeof(dctx->entropy.workspace) >= sizeof(seq_t) * STORED_SEQS);
		dctx->fseEntropy = 1;
		{
			U32 i;
			for (i = 0; i < ZSTD_REP_NUM; i++)
				seqState.prevOffset[i] = dctx->entropy.rep[i];
		}
		seqState.base = base;
		seqState.pos = (size_t)(op - base);
		seqState.gotoDict = (uPtrDiff)dictEnd - (uPtrDiff)base;  
		CHECK_E(BIT_initDStream(&seqState.DStream, ip, iend - ip), corruption_detected);
		FSE_initDState(&seqState.stateLL, &seqState.DStream, dctx->LLTptr);
		FSE_initDState(&seqState.stateOffb, &seqState.DStream, dctx->OFTptr);
		FSE_initDState(&seqState.stateML, &seqState.DStream, dctx->MLTptr);

		for (seqNb = 0; (BIT_reloadDStream(&seqState.DStream) <= BIT_DStream_completed) && seqNb < seqAdvance; seqNb++) {
			sequences[seqNb] = ZSTD_decodeSequenceLong(&seqState, windowSize);
		}
		if (seqNb < seqAdvance)
			return ERROR(corruption_detected);

		for (; (BIT_reloadDStream(&(seqState.DStream)) <= BIT_DStream_completed) && seqNb < nbSeq; seqNb++) {
			seq_t const sequence = ZSTD_decodeSequenceLong(&seqState, windowSize);
			size_t const oneSeqSize =
			    ZSTD_execSequenceLong(op, oend, sequences[(seqNb - ADVANCED_SEQS) & STOSEQ_MASK], &litPtr, litEnd, base, vBase, dictEnd);
			if (ZSTD_isError(oneSeqSize))
				return oneSeqSize;
			ZSTD_PREFETCH(sequence.match);
			sequences[seqNb & STOSEQ_MASK] = sequence;
			op += oneSeqSize;
		}
		if (seqNb < nbSeq)
			return ERROR(corruption_detected);

		seqNb -= seqAdvance;
		for (; seqNb < nbSeq; seqNb++) {
			size_t const oneSeqSize = ZSTD_execSequenceLong(op, oend, sequences[seqNb & STOSEQ_MASK], &litPtr, litEnd, base, vBase, dictEnd);
			if (ZSTD_isError(oneSeqSize))
				return oneSeqSize;
			op += oneSeqSize;
		}

		{
			U32 i;
			for (i = 0; i < ZSTD_REP_NUM; i++)
				dctx->entropy.rep[i] = (U32)(seqState.prevOffset[i]);
		}
	}

	{
		size_t const lastLLSize = litEnd - litPtr;
		if (lastLLSize > (size_t)(oend - op))
			return ERROR(dstSize_tooSmall);
		memcpy(op, litPtr, lastLLSize);
		op += lastLLSize;
	}

	return op - ostart;
}

static size_t ZSTD_decompressBlock_internal(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity, const void *src, size_t srcSize)
{  
	const BYTE *ip = (const BYTE *)src;

	if (srcSize >= ZSTD_BLOCKSIZE_ABSOLUTEMAX)
		return ERROR(srcSize_wrong);

	{
		size_t const litCSize = ZSTD_decodeLiteralsBlock(dctx, src, srcSize);
		if (ZSTD_isError(litCSize))
			return litCSize;
		ip += litCSize;
		srcSize -= litCSize;
	}
	if (sizeof(size_t) > 4)  

		if (dctx->fParams.windowSize > (1 << 23))
			return ZSTD_decompressSequencesLong(dctx, dst, dstCapacity, ip, srcSize);
	return ZSTD_decompressSequences(dctx, dst, dstCapacity, ip, srcSize);
}

/**
 * A match can reference output from an earlier destination buffer. When dst no
 * longer equals previousDstEnd, retain the old end as dictionary history and
 * shift the virtual base so match distances still span the boundary.
 */
static void ZSTD_checkContinuity(ZSTD_DCtx *dctx, const void *dst)
{
	if (dst != dctx->previousDstEnd) {  
		dctx->dictEnd = dctx->previousDstEnd;
		dctx->vBase = (const char *)dst - ((const char *)(dctx->previousDstEnd) - (const char *)(dctx->base));
		dctx->base = dst;
		dctx->previousDstEnd = dst;
	}
}

size_t ZSTD_decompressBlock(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity, const void *src, size_t srcSize)
{
	size_t dSize;
	ZSTD_checkContinuity(dctx, dst);
	dSize = ZSTD_decompressBlock_internal(dctx, dst, dstCapacity, src, srcSize);
	dctx->previousDstEnd = (char *)dst + dSize;
	return dSize;
}

size_t ZSTD_generateNxBytes(void *dst, size_t dstCapacity, BYTE byte, size_t length)
{
	if (length > dstCapacity)
		return ERROR(dstSize_tooSmall);
	memset(dst, byte, length);
	return length;
}

size_t ZSTD_findFrameCompressedSize(const void *src, size_t srcSize)
{
	if (srcSize >= ZSTD_skippableHeaderSize && (ZSTD_readLE32(src) & 0xFFFFFFF0U) == ZSTD_MAGIC_SKIPPABLE_START) {
		return ZSTD_skippableHeaderSize + ZSTD_readLE32((const BYTE *)src + 4);
	} else {
		const BYTE *ip = (const BYTE *)src;
		const BYTE *const ipstart = ip;
		size_t remainingSize = srcSize;
		ZSTD_frameParams fParams;

		size_t const headerSize = ZSTD_frameHeaderSize(ip, remainingSize);
		if (ZSTD_isError(headerSize))
			return headerSize;

		{
			size_t const ret = ZSTD_getFrameParams(&fParams, ip, remainingSize);
			if (ZSTD_isError(ret))
				return ret;
			if (ret > 0)
				return ERROR(srcSize_wrong);
		}

		ip += headerSize;
		remainingSize -= headerSize;

		while (1) {
			blockProperties_t blockProperties;
			size_t const cBlockSize = ZSTD_getcBlockSize(ip, remainingSize, &blockProperties);
			if (ZSTD_isError(cBlockSize))
				return cBlockSize;

			if (ZSTD_blockHeaderSize + cBlockSize > remainingSize)
				return ERROR(srcSize_wrong);

			ip += ZSTD_blockHeaderSize + cBlockSize;
			remainingSize -= ZSTD_blockHeaderSize + cBlockSize;

			if (blockProperties.lastBlock)
				break;
		}

		if (fParams.checksumFlag) {  
			if (remainingSize < 4)
				return ERROR(srcSize_wrong);
			ip += 4;
			remainingSize -= 4;
		}

		return ip - ipstart;
	}
}

/*
 * Consumes a frame block by block and enforces output capacity.
 * Successfully decoding a frame does not authenticate its filesystem origin.
 */
static size_t ZSTD_decompressFrame(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity, const void **srcPtr, size_t *srcSizePtr)
{
	const BYTE *ip = (const BYTE *)(*srcPtr);
	BYTE *const ostart = (BYTE * const)dst;
	BYTE *const oend = ostart + dstCapacity;
	BYTE *op = ostart;
	size_t remainingSize = *srcSizePtr;

	if (remainingSize < ZSTD_frameHeaderSize_min + ZSTD_blockHeaderSize)
		return ERROR(srcSize_wrong);

	{
		size_t const frameHeaderSize = ZSTD_frameHeaderSize(ip, ZSTD_frameHeaderSize_prefix);
		if (ZSTD_isError(frameHeaderSize))
			return frameHeaderSize;
		if (remainingSize < frameHeaderSize + ZSTD_blockHeaderSize)
			return ERROR(srcSize_wrong);
		CHECK_F(ZSTD_decodeFrameHeader(dctx, ip, frameHeaderSize));
		ip += frameHeaderSize;
		remainingSize -= frameHeaderSize;
	}

	while (1) {
		size_t decodedSize;
		blockProperties_t blockProperties;
		size_t const cBlockSize = ZSTD_getcBlockSize(ip, remainingSize, &blockProperties);
		if (ZSTD_isError(cBlockSize))
			return cBlockSize;

		ip += ZSTD_blockHeaderSize;
		remainingSize -= ZSTD_blockHeaderSize;
		if (cBlockSize > remainingSize)
			return ERROR(srcSize_wrong);

		switch (blockProperties.blockType) {
		case bt_compressed: decodedSize = ZSTD_decompressBlock_internal(dctx, op, oend - op, ip, cBlockSize); break;
		case bt_raw: decodedSize = ZSTD_copyRawBlock(op, oend - op, ip, cBlockSize); break;
		case bt_rle: decodedSize = ZSTD_generateNxBytes(op, oend - op, *ip, blockProperties.origSize); break;
		case bt_reserved:
		default: return ERROR(corruption_detected);
		}

		if (ZSTD_isError(decodedSize))
			return decodedSize;
		if (dctx->fParams.checksumFlag)
			xxh64_update(&dctx->xxhState, op, decodedSize);
		op += decodedSize;
		ip += cBlockSize;
		remainingSize -= cBlockSize;
		if (blockProperties.lastBlock)
			break;
	}

	if (dctx->fParams.checksumFlag) {  
		U32 const checkCalc = (U32)xxh64_digest(&dctx->xxhState);
		U32 checkRead;
		if (remainingSize < 4)
			return ERROR(checksum_wrong);
		checkRead = ZSTD_readLE32(ip);
		if (checkRead != checkCalc)
			return ERROR(checksum_wrong);
		ip += 4;
		remainingSize -= 4;
	}

	*srcPtr = ip;
	*srcSizePtr = remainingSize;
	return op - ostart;
}

static size_t ZSTD_decompressBegin_usingDict(ZSTD_DCtx *dctx, const void *dict, size_t dictSize);
static size_t ZSTD_decompressMultiFrame(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity, const void *src, size_t srcSize, const void *dict, size_t dictSize)
{
	void *const dststart = dst;

	while (srcSize >= ZSTD_frameHeaderSize_prefix) {
		U32 magicNumber;

		magicNumber = ZSTD_readLE32(src);
		if (magicNumber != ZSTD_MAGICNUMBER) {
			if ((magicNumber & 0xFFFFFFF0U) == ZSTD_MAGIC_SKIPPABLE_START) {
				size_t skippableSize;
				if (srcSize < ZSTD_skippableHeaderSize)
					return ERROR(srcSize_wrong);
				skippableSize = ZSTD_readLE32((const BYTE *)src + 4) + ZSTD_skippableHeaderSize;
				if (srcSize < skippableSize) {
					return ERROR(srcSize_wrong);
				}

				src = (const BYTE *)src + skippableSize;
				srcSize -= skippableSize;
				continue;
			} else {
				return ERROR(prefix_unknown);
			}
		}

		CHECK_F(ZSTD_decompressBegin_usingDict(dctx, dict, dictSize));
		ZSTD_checkContinuity(dctx, dst);

		{
			const size_t res = ZSTD_decompressFrame(dctx, dst, dstCapacity, &src, &srcSize);
			if (ZSTD_isError(res))
				return res;

			dst = (BYTE *)dst + res;
			dstCapacity -= res;
		}
	}

	if (srcSize)
		return ERROR(srcSize_wrong);  

	return (BYTE *)dst - (BYTE *)dststart;
}

size_t ZSTD_nextSrcSizeToDecompress(ZSTD_DCtx *dctx) { return dctx->expected; }

static ZSTD_nextInputType_e ZSTD_nextInputType(ZSTD_DCtx *dctx)
{
	switch (dctx->stage) {
	default:  
	case ZSTDds_getFrameHeaderSize:
	case ZSTDds_decodeFrameHeader: return ZSTDnit_frameHeader;
	case ZSTDds_decodeBlockHeader: return ZSTDnit_blockHeader;
	case ZSTDds_decompressBlock: return ZSTDnit_block;
	case ZSTDds_decompressLastBlock: return ZSTDnit_lastBlock;
	case ZSTDds_checkChecksum: return ZSTDnit_checksum;
	case ZSTDds_decodeSkippableHeader:
	case ZSTDds_skipFrame: return ZSTDnit_skippableFrame;
	}
}

int ZSTD_isSkipFrame(ZSTD_DCtx *dctx) { return dctx->stage == ZSTDds_skipFrame; }  

/*
 * Advance the streaming frame/sequence state machine. srcSize must equal the
 * state's requested expected byte count; a larger or smaller fragment is not
 * consumed opportunistically. The decoder preserves header, dictionary and
 * checksum state between calls, and the returned size may encode an error.
 * A successful stage is not necessarily the end of a complete frame.
 */
size_t ZSTD_decompressContinue(ZSTD_DCtx *dctx, void *dst, size_t dstCapacity, const void *src, size_t srcSize)
{

	if (srcSize != dctx->expected)
		return ERROR(srcSize_wrong);
	if (dstCapacity)
		ZSTD_checkContinuity(dctx, dst);

	switch (dctx->stage) {
	case ZSTDds_getFrameHeaderSize:
		if (srcSize != ZSTD_frameHeaderSize_prefix)
			return ERROR(srcSize_wrong);					 
		if ((ZSTD_readLE32(src) & 0xFFFFFFF0U) == ZSTD_MAGIC_SKIPPABLE_START) {  
			memcpy(dctx->headerBuffer, src, ZSTD_frameHeaderSize_prefix);
			dctx->expected = ZSTD_skippableHeaderSize - ZSTD_frameHeaderSize_prefix;  
			dctx->stage = ZSTDds_decodeSkippableHeader;
			return 0;
		}
		dctx->headerSize = ZSTD_frameHeaderSize(src, ZSTD_frameHeaderSize_prefix);
		if (ZSTD_isError(dctx->headerSize))
			return dctx->headerSize;
		memcpy(dctx->headerBuffer, src, ZSTD_frameHeaderSize_prefix);
		if (dctx->headerSize > ZSTD_frameHeaderSize_prefix) {
			dctx->expected = dctx->headerSize - ZSTD_frameHeaderSize_prefix;
			dctx->stage = ZSTDds_decodeFrameHeader;
			return 0;
		}
		dctx->expected = 0;  

	case ZSTDds_decodeFrameHeader:
		memcpy(dctx->headerBuffer + ZSTD_frameHeaderSize_prefix, src, dctx->expected);
		CHECK_F(ZSTD_decodeFrameHeader(dctx, dctx->headerBuffer, dctx->headerSize));
		dctx->expected = ZSTD_blockHeaderSize;
		dctx->stage = ZSTDds_decodeBlockHeader;
		return 0;

	case ZSTDds_decodeBlockHeader: {
		blockProperties_t bp;
		size_t const cBlockSize = ZSTD_getcBlockSize(src, ZSTD_blockHeaderSize, &bp);
		if (ZSTD_isError(cBlockSize))
			return cBlockSize;
		dctx->expected = cBlockSize;
		dctx->bType = bp.blockType;
		dctx->rleSize = bp.origSize;
		if (cBlockSize) {
			dctx->stage = bp.lastBlock ? ZSTDds_decompressLastBlock : ZSTDds_decompressBlock;
			return 0;
		}

		if (bp.lastBlock) {
			if (dctx->fParams.checksumFlag) {
				dctx->expected = 4;
				dctx->stage = ZSTDds_checkChecksum;
			} else {
				dctx->expected = 0;  
				dctx->stage = ZSTDds_getFrameHeaderSize;
			}
		} else {
			dctx->expected = 3;  
			dctx->stage = ZSTDds_decodeBlockHeader;
		}
		return 0;
	}
	case ZSTDds_decompressLastBlock:
	case ZSTDds_decompressBlock: {
		size_t rSize;
		switch (dctx->bType) {
		case bt_compressed: rSize = ZSTD_decompressBlock_internal(dctx, dst, dstCapacity, src, srcSize); break;
		case bt_raw: rSize = ZSTD_copyRawBlock(dst, dstCapacity, src, srcSize); break;
		case bt_rle: rSize = ZSTD_setRleBlock(dst, dstCapacity, src, srcSize, dctx->rleSize); break;
		case bt_reserved:  
		default: return ERROR(corruption_detected);
		}
		if (ZSTD_isError(rSize))
			return rSize;
		if (dctx->fParams.checksumFlag)
			xxh64_update(&dctx->xxhState, dst, rSize);

		if (dctx->stage == ZSTDds_decompressLastBlock) {  
			if (dctx->fParams.checksumFlag) {	 
				dctx->expected = 4;
				dctx->stage = ZSTDds_checkChecksum;
			} else {
				dctx->expected = 0;  
				dctx->stage = ZSTDds_getFrameHeaderSize;
			}
		} else {
			dctx->stage = ZSTDds_decodeBlockHeader;
			dctx->expected = ZSTD_blockHeaderSize;
			dctx->previousDstEnd = (char *)dst + rSize;
		}
		return rSize;
	}
        /*
         * An optional frame checksum covers the decoded bytes. Compare the
         * low 32 bits of the running XXH64 digest with the four-byte trailer
         * only after the final block.
         */
	case ZSTDds_checkChecksum: {
		U32 const h32 = (U32)xxh64_digest(&dctx->xxhState);
		U32 const check32 = ZSTD_readLE32(src);  
		if (check32 != h32)
			return ERROR(checksum_wrong);
		dctx->expected = 0;
		dctx->stage = ZSTDds_getFrameHeaderSize;
		return 0;
	}
	case ZSTDds_decodeSkippableHeader: {
		memcpy(dctx->headerBuffer + ZSTD_frameHeaderSize_prefix, src, dctx->expected);
		dctx->expected = ZSTD_readLE32(dctx->headerBuffer + 4);
		dctx->stage = ZSTDds_skipFrame;
		return 0;
	}
	case ZSTDds_skipFrame: {
		dctx->expected = 0;
		dctx->stage = ZSTDds_getFrameHeaderSize;
		return 0;
	}
	default:
		return ERROR(GENERIC);  
	}
}

static size_t ZSTD_refDictContent(ZSTD_DCtx *dctx, const void *dict, size_t dictSize)
{
	dctx->dictEnd = dctx->previousDstEnd;
	dctx->vBase = (const char *)dict - ((const char *)(dctx->previousDstEnd) - (const char *)(dctx->base));
	dctx->base = dict;
	dctx->previousDstEnd = (const char *)dict + dictSize;
	return 0;
}

static size_t ZSTD_loadEntropy(ZSTD_entropyTables_t *entropy, const void *const dict, size_t const dictSize)
{
	const BYTE *dictPtr = (const BYTE *)dict;
	const BYTE *const dictEnd = dictPtr + dictSize;

	if (dictSize <= 8)
		return ERROR(dictionary_corrupted);
	dictPtr += 8;  

	{
		size_t const hSize = HUF_readDTableX4_wksp(entropy->hufTable, dictPtr, dictEnd - dictPtr, entropy->workspace, sizeof(entropy->workspace));
		if (HUF_isError(hSize))
			return ERROR(dictionary_corrupted);
		dictPtr += hSize;
	}

	{
		short offcodeNCount[MaxOff + 1];
		U32 offcodeMaxValue = MaxOff, offcodeLog;
		size_t const offcodeHeaderSize = FSE_readNCount(offcodeNCount, &offcodeMaxValue, &offcodeLog, dictPtr, dictEnd - dictPtr);
		if (FSE_isError(offcodeHeaderSize))
			return ERROR(dictionary_corrupted);
		if (offcodeLog > OffFSELog)
			return ERROR(dictionary_corrupted);
		CHECK_E(FSE_buildDTable_wksp(entropy->OFTable, offcodeNCount, offcodeMaxValue, offcodeLog, entropy->workspace, sizeof(entropy->workspace)), dictionary_corrupted);
		dictPtr += offcodeHeaderSize;
	}

	{
		short matchlengthNCount[MaxML + 1];
		unsigned matchlengthMaxValue = MaxML, matchlengthLog;
		size_t const matchlengthHeaderSize = FSE_readNCount(matchlengthNCount, &matchlengthMaxValue, &matchlengthLog, dictPtr, dictEnd - dictPtr);
		if (FSE_isError(matchlengthHeaderSize))
			return ERROR(dictionary_corrupted);
		if (matchlengthLog > MLFSELog)
			return ERROR(dictionary_corrupted);
		CHECK_E(FSE_buildDTable_wksp(entropy->MLTable, matchlengthNCount, matchlengthMaxValue, matchlengthLog, entropy->workspace, sizeof(entropy->workspace)), dictionary_corrupted);
		dictPtr += matchlengthHeaderSize;
	}

	{
		short litlengthNCount[MaxLL + 1];
		unsigned litlengthMaxValue = MaxLL, litlengthLog;
		size_t const litlengthHeaderSize = FSE_readNCount(litlengthNCount, &litlengthMaxValue, &litlengthLog, dictPtr, dictEnd - dictPtr);
		if (FSE_isError(litlengthHeaderSize))
			return ERROR(dictionary_corrupted);
		if (litlengthLog > LLFSELog)
			return ERROR(dictionary_corrupted);
		CHECK_E(FSE_buildDTable_wksp(entropy->LLTable, litlengthNCount, litlengthMaxValue, litlengthLog, entropy->workspace, sizeof(entropy->workspace)), dictionary_corrupted);
		dictPtr += litlengthHeaderSize;
	}

	if (dictPtr + 12 > dictEnd)
		return ERROR(dictionary_corrupted);
	{
		int i;
		size_t const dictContentSize = (size_t)(dictEnd - (dictPtr + 12));
		for (i = 0; i < 3; i++) {
			U32 const rep = ZSTD_readLE32(dictPtr);
			dictPtr += 4;
			if (rep == 0 || rep >= dictContentSize)
				return ERROR(dictionary_corrupted);
			entropy->rep[i] = rep;
		}
	}

	return dictPtr - (const BYTE *)dict;
}

static size_t ZSTD_decompress_insertDictionary(ZSTD_DCtx *dctx, const void *dict, size_t dictSize)
{
	if (dictSize < 8)
		return ZSTD_refDictContent(dctx, dict, dictSize);
	{
		U32 const magic = ZSTD_readLE32(dict);
		if (magic != ZSTD_DICT_MAGIC) {
			return ZSTD_refDictContent(dctx, dict, dictSize);  
		}
	}
	dctx->dictID = ZSTD_readLE32((const char *)dict + 4);

	{
		size_t const eSize = ZSTD_loadEntropy(&dctx->entropy, dict, dictSize);
		if (ZSTD_isError(eSize))
			return ERROR(dictionary_corrupted);
		dict = (const char *)dict + eSize;
		dictSize -= eSize;
	}
	dctx->litEntropy = dctx->fseEntropy = 1;

	return ZSTD_refDictContent(dctx, dict, dictSize);
}

static size_t ZSTD_decompressBegin_usingDict(ZSTD_DCtx *dctx, const void *dict, size_t dictSize)
{
	CHECK_F(ZSTD_decompressBegin(dctx));
	if (dict && dictSize)
		CHECK_E(ZSTD_decompress_insertDictionary(dctx, dict, dictSize), dictionary_corrupted);
	return 0;
}

typedef enum { zdss_init, zdss_loadHeader, zdss_read, zdss_load, zdss_flush } ZSTD_dStreamStage;

struct ZSTD_DStream_s {
	ZSTD_DCtx *dctx;
	ZSTD_frameParams fParams;
	ZSTD_dStreamStage stage;
	char *inBuff;
	size_t inBuffSize;
	size_t inPos;
	size_t maxWindowSize;
	char *outBuff;
	size_t outBuffSize;
	size_t outStart;
	size_t outEnd;
	size_t blockSize;
	BYTE headerBuffer[ZSTD_FRAMEHEADERSIZE_MAX];  
	size_t lhSize;
	U32 hostageByte;
};  

typedef struct ZSTD_DStreamWorkspace {
	ZSTD_DStream DStream;
	ZSTD_DCtx DCtx;
	char Buffer[0];
} ZSTD_DStreamWorkspace;

size_t ZSTD_DStreamWorkspaceBound(size_t maxWindowSize)
{
	size_t const blockSize = MIN(maxWindowSize, ZSTD_BLOCKSIZE_ABSOLUTEMAX);
	size_t const inBuffSize = blockSize;
	size_t const outBuffSize = maxWindowSize + blockSize + WILDCOPY_OVERLENGTH * 2;
	return sizeof(ZSTD_DStreamWorkspace) + inBuffSize + outBuffSize;
}

ZSTD_DStream *ZSTD_initDStream(size_t maxWindowSize, void *workspace, size_t workspaceSize)
{
	ZSTD_DStreamWorkspace *ws = (ZSTD_DStreamWorkspace *)workspace;
	ZSTD_DStream *zds = &ws->DStream;
	memset(zds, 0, sizeof(ZSTD_DStream));
	zds->dctx = &ws->DCtx;
	ZSTD_decompressBegin(zds->dctx);
	zds->maxWindowSize = maxWindowSize;
	zds->stage = zdss_loadHeader;
	zds->lhSize = zds->inPos = zds->outStart = zds->outEnd = 0;
	zds->hostageByte = 0;

	size_t const blockSize = MIN(zds->maxWindowSize, ZSTD_BLOCKSIZE_ABSOLUTEMAX);
	size_t const neededOutSize = zds->maxWindowSize + blockSize + WILDCOPY_OVERLENGTH * 2;

	zds->inBuff = ws->Buffer;
	zds->inBuffSize = blockSize;
	zds->outBuff = ws->Buffer + blockSize;
	zds->outBuffSize = neededOutSize;
	return zds;
}

size_t ZSTD_resetDStream(ZSTD_DStream *zds)
{
	zds->stage = zdss_loadHeader;
	zds->lhSize = zds->inPos = zds->outStart = zds->outEnd = 0;
	zds->hostageByte = 0;
	return ZSTD_frameHeaderSize_prefix;
}

ZSTD_STATIC size_t ZSTD_limitCopy(void *dst, size_t dstCapacity, const void *src, size_t srcSize)
{
	size_t const length = MIN(dstCapacity, srcSize);
	memcpy(dst, src, length);
	return length;
}

/*
 * Drive the public pull/push streaming interface using the internal frame
 * state. Input and output pos fields are mutable cursors, not total-size
 * return values; the function may consume input or produce output without
 * completing a frame. The caller must check the returned error/status and
 * retain context across partial calls.
 */
size_t ZSTD_decompressStream(ZSTD_DStream *zds, ZSTD_outBuffer *output, ZSTD_inBuffer *input)
{
	const char *const istart = (const char *)(input->src) + input->pos;
	const char *const iend = (const char *)(input->src) + input->size;
	const char *ip = istart;
	char *const ostart = (char *)(output->dst) + output->pos;
	char *const oend = (char *)(output->dst) + output->size;
	char *op = ostart;
	U32 someMoreWork = 1;

	while (someMoreWork) {
		switch (zds->stage) {
		case zdss_init:
			ZSTD_resetDStream(zds);  

		case zdss_loadHeader: {
			size_t const hSize = ZSTD_getFrameParams(&zds->fParams, zds->headerBuffer, zds->lhSize);
			if (ZSTD_isError(hSize))
				return hSize;
			if (hSize != 0) {				    
				size_t const toLoad = hSize - zds->lhSize;  
				if (toLoad > (size_t)(iend - ip)) {	 
					memcpy(zds->headerBuffer + zds->lhSize, ip, iend - ip);
					zds->lhSize += iend - ip;
					input->pos = input->size;
					return (MAX(ZSTD_frameHeaderSize_min, hSize) - zds->lhSize) +
					       ZSTD_blockHeaderSize;  
				}
				memcpy(zds->headerBuffer + zds->lhSize, ip, toLoad);
				zds->lhSize = hSize;
				ip += toLoad;
				break;
			}

			if (zds->fParams.frameContentSize && zds->fParams.windowSize  
			    && (U64)(size_t)(oend - op) >= zds->fParams.frameContentSize) {
				size_t const cSize = ZSTD_findFrameCompressedSize(istart, iend - istart);
				if (cSize <= (size_t)(iend - istart)) {
					size_t const decompressedSize = ZSTD_decompressMultiFrame(zds->dctx, op, oend - op, istart, cSize, NULL, 0);
					if (ZSTD_isError(decompressedSize))
						return decompressedSize;
					ip = istart + cSize;
					op += decompressedSize;
					zds->dctx->expected = 0;
					zds->stage = zdss_init;
					someMoreWork = 0;
					break;
				}
			}

			ZSTD_decompressBegin(zds->dctx);
			{
				size_t const h1Size = ZSTD_nextSrcSizeToDecompress(zds->dctx);  
				CHECK_F(ZSTD_decompressContinue(zds->dctx, NULL, 0, zds->headerBuffer, h1Size));
				{
					size_t const h2Size = ZSTD_nextSrcSizeToDecompress(zds->dctx);
					CHECK_F(ZSTD_decompressContinue(zds->dctx, NULL, 0, zds->headerBuffer + h1Size, h2Size));
				}
			}

			zds->fParams.windowSize = MAX(zds->fParams.windowSize, 1U << ZSTD_WINDOWLOG_ABSOLUTEMIN);
			if (zds->fParams.windowSize > zds->maxWindowSize)
				return ERROR(frameParameter_windowTooLarge);

			zds->stage = zdss_read;
		}

		case zdss_read: {
			size_t const neededInSize = ZSTD_nextSrcSizeToDecompress(zds->dctx);
			if (neededInSize == 0) {  
				zds->stage = zdss_init;
				someMoreWork = 0;
				break;
			}
			if ((size_t)(iend - ip) >= neededInSize) {  
				const int isSkipFrame = ZSTD_isSkipFrame(zds->dctx);
				size_t const decodedSize = ZSTD_decompressContinue(zds->dctx, zds->outBuff + zds->outStart,
										   (isSkipFrame ? 0 : zds->outBuffSize - zds->outStart), ip, neededInSize);
				if (ZSTD_isError(decodedSize))
					return decodedSize;
				ip += neededInSize;
				if (!decodedSize && !isSkipFrame)
					break;  
				zds->outEnd = zds->outStart + decodedSize;
				zds->stage = zdss_flush;
				break;
			}
			if (ip == iend) {
				someMoreWork = 0;
				break;
			}  
			zds->stage = zdss_load;

		}

		case zdss_load: {
			size_t const neededInSize = ZSTD_nextSrcSizeToDecompress(zds->dctx);
			size_t const toLoad = neededInSize - zds->inPos;  
			size_t loadedSize;
			if (toLoad > zds->inBuffSize - zds->inPos)
				return ERROR(corruption_detected);  
			loadedSize = ZSTD_limitCopy(zds->inBuff + zds->inPos, toLoad, ip, iend - ip);
			ip += loadedSize;
			zds->inPos += loadedSize;
			if (loadedSize < toLoad) {
				someMoreWork = 0;
				break;
			}  

			{
				const int isSkipFrame = ZSTD_isSkipFrame(zds->dctx);
				size_t const decodedSize = ZSTD_decompressContinue(zds->dctx, zds->outBuff + zds->outStart, zds->outBuffSize - zds->outStart,
										   zds->inBuff, neededInSize);
				if (ZSTD_isError(decodedSize))
					return decodedSize;
				zds->inPos = 0;  
				if (!decodedSize && !isSkipFrame) {
					zds->stage = zdss_read;
					break;
				}  
				zds->outEnd = zds->outStart + decodedSize;
				zds->stage = zdss_flush;

			}
		}

		case zdss_flush: {
			size_t const toFlushSize = zds->outEnd - zds->outStart;
			size_t const flushedSize = ZSTD_limitCopy(op, oend - op, zds->outBuff + zds->outStart, toFlushSize);
			op += flushedSize;
			zds->outStart += flushedSize;
			if (flushedSize == toFlushSize) {  
				zds->stage = zdss_read;
				if (zds->outStart + zds->blockSize > zds->outBuffSize)
					zds->outStart = zds->outEnd = 0;
				break;
			}

			someMoreWork = 0;
			break;
		}
		default:
			return ERROR(GENERIC);  
		}
	}

	input->pos += (size_t)(ip - istart);
	output->pos += (size_t)(op - ostart);
	{
		size_t nextSrcSizeHint = ZSTD_nextSrcSizeToDecompress(zds->dctx);
		if (!nextSrcSizeHint) {			     
			if (zds->outEnd == zds->outStart) {  
				if (zds->hostageByte) {
					if (input->pos >= input->size) {
						zds->stage = zdss_read;
						return 1;
					}	      
					input->pos++;  
				}
				return 0;
			}
			if (!zds->hostageByte) {  
				input->pos--;     
				zds->hostageByte = 1;
			}
			return 1;
		}
		nextSrcSizeHint += ZSTD_blockHeaderSize * (ZSTD_nextInputType(zds->dctx) == ZSTDnit_block);  
		if (zds->inPos > nextSrcSizeHint)
			return ERROR(GENERIC);  
		nextSrcSizeHint -= zds->inPos;  
		return nextSrcSizeHint;
	}
}
