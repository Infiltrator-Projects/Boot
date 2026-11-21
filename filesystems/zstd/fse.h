 






































#ifndef FSE_H
#define FSE_H

 



 


#define FSE_PUBLIC_API

 
#define FSE_VERSION_MAJOR 0
#define FSE_VERSION_MINOR 9
#define FSE_VERSION_RELEASE 0

#define FSE_LIB_VERSION FSE_VERSION_MAJOR.FSE_VERSION_MINOR.FSE_VERSION_RELEASE
#define FSE_QUOTE(str) #str
#define FSE_EXPAND_AND_QUOTE(str) FSE_QUOTE(str)
#define FSE_VERSION_STRING FSE_EXPAND_AND_QUOTE(FSE_LIB_VERSION)

#define FSE_VERSION_NUMBER (FSE_VERSION_MAJOR * 100 * 100 + FSE_VERSION_MINOR * 100 + FSE_VERSION_RELEASE)
FSE_PUBLIC_API unsigned FSE_versionNumber(void);  

 


FSE_PUBLIC_API size_t FSE_compressBound(size_t size);  

 
FSE_PUBLIC_API unsigned FSE_isError(size_t code);  

 


 

















 
 



FSE_PUBLIC_API unsigned FSE_optimalTableLog(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue);

 




FSE_PUBLIC_API size_t FSE_normalizeCount(short *normalizedCounter, unsigned tableLog, const unsigned *count, size_t srcSize, unsigned maxSymbolValue);

 


FSE_PUBLIC_API size_t FSE_NCountWriteBound(unsigned maxSymbolValue, unsigned tableLog);

 



FSE_PUBLIC_API size_t FSE_writeNCount(void *buffer, size_t bufferSize, const short *normalizedCounter, unsigned maxSymbolValue, unsigned tableLog);

 

typedef unsigned FSE_CTable;  

 




FSE_PUBLIC_API size_t FSE_compress_usingCTable(void *dst, size_t dstCapacity, const void *src, size_t srcSize, const FSE_CTable *ct);

 










































 

 




FSE_PUBLIC_API size_t FSE_readNCount(short *normalizedCounter, unsigned *maxSymbolValuePtr, unsigned *tableLogPtr, const void *rBuffer, size_t rBuffSize);

 

typedef unsigned FSE_DTable;  

 


FSE_PUBLIC_API size_t FSE_buildDTable_wksp(FSE_DTable *dt, const short *normalizedCounter, unsigned maxSymbolValue, unsigned tableLog, void *workspace, size_t workspaceSize);

 




FSE_PUBLIC_API size_t FSE_decompress_usingDTable(void *dst, size_t dstCapacity, const void *cSrc, size_t cSrcSize, const FSE_DTable *dt);

 



























 
#include "bitstream.h"

 


 
#define FSE_NCOUNTBOUND 512
#define FSE_BLOCKBOUND(size) (size + (size >> 7))
#define FSE_COMPRESSBOUND(size) (FSE_NCOUNTBOUND + FSE_BLOCKBOUND(size))  

 
#define FSE_CTABLE_SIZE_U32(maxTableLog, maxSymbolValue) (1 + (1 << (maxTableLog - 1)) + ((maxSymbolValue + 1) * 2))
#define FSE_DTABLE_SIZE_U32(maxTableLog) (1 + (1 << maxTableLog))

 


 



size_t FSE_count_wksp(unsigned *count, unsigned *maxSymbolValuePtr, const void *source, size_t sourceSize, unsigned *workSpace);

 



size_t FSE_countFast_wksp(unsigned *count, unsigned *maxSymbolValuePtr, const void *src, size_t srcSize, unsigned *workSpace);

 



size_t FSE_count_simple(unsigned *count, unsigned *maxSymbolValuePtr, const void *src, size_t srcSize);

unsigned FSE_optimalTableLog_internal(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue, unsigned minus);
 

size_t FSE_buildCTable_raw(FSE_CTable *ct, unsigned nbBits);
 

size_t FSE_buildCTable_rle(FSE_CTable *ct, unsigned char symbolValue);
 

 



size_t FSE_buildCTable_wksp(FSE_CTable *ct, const short *normalizedCounter, unsigned maxSymbolValue, unsigned tableLog, void *workSpace, size_t wkspSize);

size_t FSE_buildDTable_raw(FSE_DTable *dt, unsigned nbBits);
 

size_t FSE_buildDTable_rle(FSE_DTable *dt, unsigned char symbolValue);
 

size_t FSE_decompress_wksp(void *dst, size_t dstCapacity, const void *cSrc, size_t cSrcSize, unsigned maxLog, void *workspace, size_t workspaceSize);
 


 


typedef struct {
	size_t state;
	const void *table;  
} FSE_DState_t;

static void FSE_initDState(FSE_DState_t *DStatePtr, BIT_DStream_t *bitD, const FSE_DTable *dt);

static unsigned char FSE_decodeSymbol(FSE_DState_t *DStatePtr, BIT_DStream_t *bitD);

static unsigned FSE_endOfDState(const FSE_DState_t *DStatePtr);

 
















































 


static unsigned char FSE_decodeSymbolFast(FSE_DState_t *DStatePtr, BIT_DStream_t *bitD);
 

 

typedef struct {
	U16 tableLog;
	U16 fastMode;
} FSE_DTableHeader;  

typedef struct {
	unsigned short newState;
	unsigned char symbol;
	unsigned char nbBits;
} FSE_decode_t;  

ZSTD_STATIC void FSE_initDState(FSE_DState_t *DStatePtr, BIT_DStream_t *bitD, const FSE_DTable *dt)
{
	const void *ptr = dt;
	const FSE_DTableHeader *const DTableH = (const FSE_DTableHeader *)ptr;
	DStatePtr->state = BIT_readBits(bitD, DTableH->tableLog);
	BIT_reloadDStream(bitD);
	DStatePtr->table = dt + 1;
}

ZSTD_STATIC BYTE FSE_peekSymbol(const FSE_DState_t *DStatePtr)
{
	FSE_decode_t const DInfo = ((const FSE_decode_t *)(DStatePtr->table))[DStatePtr->state];
	return DInfo.symbol;
}

ZSTD_STATIC void FSE_updateState(FSE_DState_t *DStatePtr, BIT_DStream_t *bitD)
{
	FSE_decode_t const DInfo = ((const FSE_decode_t *)(DStatePtr->table))[DStatePtr->state];
	U32 const nbBits = DInfo.nbBits;
	size_t const lowBits = BIT_readBits(bitD, nbBits);
	DStatePtr->state = DInfo.newState + lowBits;
}

ZSTD_STATIC BYTE FSE_decodeSymbol(FSE_DState_t *DStatePtr, BIT_DStream_t *bitD)
{
	FSE_decode_t const DInfo = ((const FSE_decode_t *)(DStatePtr->table))[DStatePtr->state];
	U32 const nbBits = DInfo.nbBits;
	BYTE const symbol = DInfo.symbol;
	size_t const lowBits = BIT_readBits(bitD, nbBits);

	DStatePtr->state = DInfo.newState + lowBits;
	return symbol;
}

 

ZSTD_STATIC BYTE FSE_decodeSymbolFast(FSE_DState_t *DStatePtr, BIT_DStream_t *bitD)
{
	FSE_decode_t const DInfo = ((const FSE_decode_t *)(DStatePtr->table))[DStatePtr->state];
	U32 const nbBits = DInfo.nbBits;
	BYTE const symbol = DInfo.symbol;
	size_t const lowBits = BIT_readBitsFast(bitD, nbBits);

	DStatePtr->state = DInfo.newState + lowBits;
	return symbol;
}

ZSTD_STATIC unsigned FSE_endOfDState(const FSE_DState_t *DStatePtr) { return DStatePtr->state == 0; }

 


 




#ifndef FSE_MAX_MEMORY_USAGE
#define FSE_MAX_MEMORY_USAGE 14
#endif
#ifndef FSE_DEFAULT_MEMORY_USAGE
#define FSE_DEFAULT_MEMORY_USAGE 13
#endif

 


#ifndef FSE_MAX_SYMBOL_VALUE
#define FSE_MAX_SYMBOL_VALUE 255
#endif

 


#define FSE_FUNCTION_TYPE BYTE
#define FSE_FUNCTION_EXTENSION
#define FSE_DECODE_TYPE FSE_decode_t

 


#define FSE_MAX_TABLELOG (FSE_MAX_MEMORY_USAGE - 2)
#define FSE_MAX_TABLESIZE (1U << FSE_MAX_TABLELOG)
#define FSE_MAXTABLESIZE_MASK (FSE_MAX_TABLESIZE - 1)
#define FSE_DEFAULT_TABLELOG (FSE_DEFAULT_MEMORY_USAGE - 2)
#define FSE_MIN_TABLELOG 5

#define FSE_TABLELOG_ABSOLUTE_MAX 15
#if FSE_MAX_TABLELOG > FSE_TABLELOG_ABSOLUTE_MAX
#error "FSE_MAX_TABLELOG > FSE_TABLELOG_ABSOLUTE_MAX is not supported"
#endif

#define FSE_TABLESTEP(tableSize) ((tableSize >> 1) + (tableSize >> 3) + 3)

#endif  
