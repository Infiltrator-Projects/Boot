/*
 * Zstandard Huffman decoding table layouts and entry points. Decode table
 * selection affects output byte stride and workspace size; all inputs are
 * untrusted until their entropy headers and bounds are validated.
 */
 






































#ifndef HUF_H_298734234
#define HUF_H_298734234

 

 
#define HUF_BLOCKSIZE_MAX (128 * 1024)  
size_t HUF_compressBound(size_t size);  

 
unsigned HUF_isError(size_t code);  

 

 

size_t HUF_compress4X_wksp(void *dst, size_t dstSize, const void *src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog, void *workSpace,
			   size_t wkspSize);  

 
#include "mem.h"  

 
#define HUF_TABLELOG_MAX 12      
#define HUF_TABLELOG_DEFAULT 11  
#define HUF_SYMBOLVALUE_MAX 255

#define HUF_TABLELOG_ABSOLUTEMAX 15  
#if (HUF_TABLELOG_MAX > HUF_TABLELOG_ABSOLUTEMAX)
#error "HUF_TABLELOG_MAX is too large !"
#endif

 


 
#define HUF_CTABLEBOUND 129
#define HUF_BLOCKBOUND(size) (size + (size >> 8) + 8)			  
#define HUF_COMPRESSBOUND(size) (HUF_CTABLEBOUND + HUF_BLOCKBOUND(size))  

 
#define HUF_CREATE_STATIC_CTABLE(name, maxSymbolValue) \
	U32 name##hb[maxSymbolValue + 1];              \
	void *name##hv = &(name##hb);                  \
	HUF_CElt *name = (HUF_CElt *)(name##hv)  

 
typedef U32 HUF_DTable;
/*
 * Reserve the descriptor word as well as the entries indexed by a prefix of
 * the bitstream. The X2 and X4 layouts pack different amounts of decoded
 * output into each lookup.
 */
#define HUF_DTABLE_SIZE(maxTableLog) (1 + (1 << (maxTableLog)))
#define HUF_CREATE_STATIC_DTABLEX2(DTable, maxTableLog) HUF_DTable DTable[HUF_DTABLE_SIZE((maxTableLog)-1)] = {((U32)((maxTableLog)-1) * 0x01000001)}
#define HUF_CREATE_STATIC_DTABLEX4(DTable, maxTableLog) HUF_DTable DTable[HUF_DTABLE_SIZE(maxTableLog)] = {((U32)(maxTableLog)*0x01000001)}

 
#define HUF_COMPRESS_WORKSPACE_SIZE (6 << 10)
#define HUF_COMPRESS_WORKSPACE_SIZE_U32 (HUF_COMPRESS_WORKSPACE_SIZE / sizeof(U32))

 
#define HUF_DECOMPRESS_WORKSPACE_SIZE (3 << 10)
#define HUF_DECOMPRESS_WORKSPACE_SIZE_U32 (HUF_DECOMPRESS_WORKSPACE_SIZE / sizeof(U32))

 


size_t HUF_decompress4X_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace, size_t workspaceSize);  
size_t HUF_decompress4X_hufOnly_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace,
				size_t workspaceSize);							        
size_t HUF_decompress4X2_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace,
				   size_t workspaceSize);  
size_t HUF_decompress4X4_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace,
				   size_t workspaceSize);  

 


 











 
unsigned HUF_optimalTableLog(unsigned maxTableLog, size_t srcSize, unsigned maxSymbolValue);
typedef struct HUF_CElt_s HUF_CElt;  
size_t HUF_writeCTable_wksp(void *dst, size_t maxDstSize, const HUF_CElt *CTable, unsigned maxSymbolValue, unsigned huffLog, void *workspace, size_t workspaceSize);
size_t HUF_compress4X_usingCTable(void *dst, size_t dstSize, const void *src, size_t srcSize, const HUF_CElt *CTable);

typedef enum {
	HUF_repeat_none,   
	HUF_repeat_check,  

	HUF_repeat_valid   
} HUF_repeat;
 




size_t HUF_compress4X_repeat(void *dst, size_t dstSize, const void *src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog, void *workSpace,
			     size_t wkspSize, HUF_CElt *hufTable, HUF_repeat *repeat,
			     int preferRepeat);  

 



size_t HUF_buildCTable_wksp(HUF_CElt *tree, const U32 *count, U32 maxSymbolValue, U32 maxNbBits, void *workSpace, size_t wkspSize);

 




size_t HUF_readStats_wksp(BYTE *huffWeight, size_t hwSize, U32 *rankStats, U32 *nbSymbolsPtr, U32 *tableLogPtr, const void *src, size_t srcSize,
			  void *workspace, size_t workspaceSize);

 

size_t HUF_readCTable_wksp(HUF_CElt *CTable, unsigned maxSymbolValue, const void *src, size_t srcSize, void *workspace, size_t workspaceSize);

 






 




U32 HUF_selectDecoder(size_t dstSize, size_t cSrcSize);

size_t HUF_readDTableX2_wksp(HUF_DTable *DTable, const void *src, size_t srcSize, void *workspace, size_t workspaceSize);
size_t HUF_readDTableX4_wksp(HUF_DTable *DTable, const void *src, size_t srcSize, void *workspace, size_t workspaceSize);

size_t HUF_decompress4X_usingDTable(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable);
size_t HUF_decompress4X2_usingDTable(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable);
size_t HUF_decompress4X4_usingDTable(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable);

 

size_t HUF_compress1X_wksp(void *dst, size_t dstSize, const void *src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog, void *workSpace,
			   size_t wkspSize);  
size_t HUF_compress1X_usingCTable(void *dst, size_t dstSize, const void *src, size_t srcSize, const HUF_CElt *CTable);
 




size_t HUF_compress1X_repeat(void *dst, size_t dstSize, const void *src, size_t srcSize, unsigned maxSymbolValue, unsigned tableLog, void *workSpace,
			     size_t wkspSize, HUF_CElt *hufTable, HUF_repeat *repeat,
			     int preferRepeat);  

size_t HUF_decompress1X_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace, size_t workspaceSize);
size_t HUF_decompress1X2_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace,
				   size_t workspaceSize);  
size_t HUF_decompress1X4_DCtx_wksp(HUF_DTable *dctx, void *dst, size_t dstSize, const void *cSrc, size_t cSrcSize, void *workspace,
				   size_t workspaceSize);  

size_t HUF_decompress1X_usingDTable(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize,
				    const HUF_DTable *DTable);  
size_t HUF_decompress1X2_usingDTable(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable);
size_t HUF_decompress1X4_usingDTable(void *dst, size_t maxDstSize, const void *cSrc, size_t cSrcSize, const HUF_DTable *DTable);

#endif  
