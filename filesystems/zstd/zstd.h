/*
 * Decoder API and decompression context contracts. Destination capacity,
 * dictionary metadata and declared frame length constrain decoding; a
 * filesystem extent must never be trusted solely on successful frame parsing.
 */
 















#ifndef ZSTD_H
#define ZSTD_H

 


 

















 

 





typedef enum {
	ZSTD_error_no_error,
	ZSTD_error_GENERIC,
	ZSTD_error_prefix_unknown,
	ZSTD_error_frameParameter_unsupported,
	ZSTD_error_frameParameter_windowTooLarge,
	ZSTD_error_dstSize_tooSmall,
	ZSTD_error_srcSize_wrong,
	ZSTD_error_corruption_detected,
	ZSTD_error_checksum_wrong,
	ZSTD_error_tableLog_tooLarge,
	ZSTD_error_maxSymbolValue_tooLarge,
	ZSTD_error_maxSymbolValue_tooSmall,
	ZSTD_error_dictionary_corrupted,
	ZSTD_error_dictionary_wrong,
	ZSTD_error_maxCode
} ZSTD_ErrorCode;

 




int ZSTD_maxCLevel(void);
 





size_t ZSTD_compressBound(size_t srcSize);
 
/*
 * Several streaming operations return a size hint on success or an encoded
 * error in the same unsigned type. The input and output pos fields separately
 * report actual progress.
 */
static __attribute__((unused)) unsigned int ZSTD_isError(size_t code)
{
	return code > (size_t)-ZSTD_error_maxCode;
}
 






static __attribute__((unused)) ZSTD_ErrorCode ZSTD_getErrorCode(
	size_t functionResult)
{
	if (!ZSTD_isError(functionResult))
		return (ZSTD_ErrorCode)0;
	return (ZSTD_ErrorCode)(0 - functionResult);
}

 




typedef enum {
	ZSTD_fast,
	ZSTD_dfast,
	ZSTD_greedy,
	ZSTD_lazy,
	ZSTD_lazy2,
	ZSTD_btlazy2,
	ZSTD_btopt,
	ZSTD_btopt2
} ZSTD_strategy;

 














typedef struct {
	unsigned int windowLog;
	unsigned int chainLog;
	unsigned int hashLog;
	unsigned int searchLog;
	unsigned int searchLength;
	unsigned int targetLength;
	ZSTD_strategy strategy;
} ZSTD_compressionParameters;

 










typedef struct {
	unsigned int contentSizeFlag;
	unsigned int checksumFlag;
	unsigned int noDictIDFlag;
} ZSTD_frameParameters;

 




typedef struct {
	ZSTD_compressionParameters cParams;
	ZSTD_frameParameters fParams;
} ZSTD_parameters;

 







ZSTD_compressionParameters ZSTD_getCParams(int compressionLevel,
	unsigned long long estimatedSrcSize, size_t dictSize);

 










ZSTD_parameters ZSTD_getParams(int compressionLevel,
	unsigned long long estimatedSrcSize, size_t dictSize);

 



 






typedef struct ZSTD_inBuffer_s {
	const void *src;
	size_t size;
	size_t pos;
} ZSTD_inBuffer;

 






typedef struct ZSTD_outBuffer_s {
	void *dst;
	size_t size;
	size_t pos;
} ZSTD_outBuffer;


 
















 






size_t ZSTD_DStreamWorkspaceBound(size_t maxWindowSize);

 


typedef struct ZSTD_DStream_s ZSTD_DStream;
 
 










ZSTD_DStream *ZSTD_initDStream(size_t maxWindowSize, void *workspace,
	size_t workspaceSize);

 
 








size_t ZSTD_resetDStream(ZSTD_DStream *zds);
 



















size_t ZSTD_decompressStream(ZSTD_DStream *zds, ZSTD_outBuffer *output,
	ZSTD_inBuffer *input);

 




size_t ZSTD_DStreamInSize(void);
 







size_t ZSTD_DStreamOutSize(void);


 
#define ZSTD_MAGICNUMBER            0xFD2FB528    
#define ZSTD_MAGIC_SKIPPABLE_START  0x184D2A50U

#define ZSTD_CONTENTSIZE_UNKNOWN (0ULL - 1)
#define ZSTD_CONTENTSIZE_ERROR   (0ULL - 2)

#define ZSTD_WINDOWLOG_MAX_32  27
#define ZSTD_WINDOWLOG_MAX_64  27
#define ZSTD_WINDOWLOG_MAX \
	((unsigned int)(sizeof(size_t) == 4 \
		? ZSTD_WINDOWLOG_MAX_32 \
		: ZSTD_WINDOWLOG_MAX_64))
#define ZSTD_WINDOWLOG_MIN 10
#define ZSTD_HASHLOG_MAX ZSTD_WINDOWLOG_MAX
#define ZSTD_HASHLOG_MIN        6
#define ZSTD_CHAINLOG_MAX     (ZSTD_WINDOWLOG_MAX+1)
#define ZSTD_CHAINLOG_MIN      ZSTD_HASHLOG_MIN
#define ZSTD_HASHLOG3_MAX      17
#define ZSTD_SEARCHLOG_MAX    (ZSTD_WINDOWLOG_MAX-1)
#define ZSTD_SEARCHLOG_MIN      1
 
#define ZSTD_SEARCHLENGTH_MAX   7
 
#define ZSTD_SEARCHLENGTH_MIN   3
#define ZSTD_TARGETLENGTH_MIN   4
#define ZSTD_TARGETLENGTH_MAX 999

 
#define ZSTD_FRAMEHEADERSIZE_MAX 18
#define ZSTD_FRAMEHEADERSIZE_MIN  6
static const size_t ZSTD_frameHeaderSize_prefix = 5;
static const size_t ZSTD_frameHeaderSize_min = ZSTD_FRAMEHEADERSIZE_MIN;
static const size_t ZSTD_frameHeaderSize_max = ZSTD_FRAMEHEADERSIZE_MAX;
 
static const size_t ZSTD_skippableHeaderSize = 8;


 



 










size_t ZSTD_findFrameCompressedSize(const void *src, size_t srcSize);

 


 






typedef struct {
	unsigned long long frameContentSize;
	unsigned int windowSize;
	unsigned int dictID;
	unsigned int checksumFlag;
} ZSTD_frameParams;

 










size_t ZSTD_getFrameParams(ZSTD_frameParams *fparamsPtr, const void *src,
	size_t srcSize);

 




























 
#define ZSTD_BLOCKSIZE_ABSOLUTEMAX (128 * 1024)

#endif   
