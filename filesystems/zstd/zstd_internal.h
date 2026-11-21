/*
 * Internal Zstandard decode macros, copies and memory helpers. Optimised
 * wild-copy operations may depend on spare buffer space; their call sites
 * must prove any overread/write allowance rather than assume host padding.
 */
 















#ifndef ZSTD_CCOMMON_H_MODULE
#define ZSTD_CCOMMON_H_MODULE

 


#define FORCE_INLINE static __always_inline
#define FORCE_NOINLINE static  

 


#include "error_private.h"
#include "mem.h"
#include "xxhash.h"
#include "zstd.h"

 


#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
/*
 * Evaluate a size-returning operation once. An encoded error leaves the
 * enclosing decoder immediately, while a successful size can be discarded
 * when the caller only needs its side effects.
 */
#define CHECK_F(f)                       \
	{                                \
		size_t const errcod = f; \
		if (ERR_isError(errcod)) \
			return errcod;   \
	}  
#define CHECK_E(f, e)                    \
	{                                \
		size_t const errcod = f; \
		if (ZSTD_isError(errcod)) \
			return ERROR(e); \
	}  
#define ZSTD_STATIC_ASSERT(c)                                   \
	{                                                       \
		enum { ZSTD_static_assert = 1 / (int)(!!(c)) }; \
	}

 


#define ZSTD_OPT_NUM (1 << 12)
#define ZSTD_DICT_MAGIC 0xEC30A437  

#define ZSTD_REP_NUM 3		       
#define ZSTD_REP_CHECK (ZSTD_REP_NUM)  
#define ZSTD_REP_MOVE (ZSTD_REP_NUM - 1)
#define ZSTD_REP_MOVE_OPT (ZSTD_REP_NUM)
static const U32 repStartValue[ZSTD_REP_NUM] = {1, 4, 8};

#define ZSTD_WINDOWLOG_ABSOLUTEMIN 10
static const size_t ZSTD_fcs_fieldSize[4] = {0, 2, 4, 8};
static const size_t ZSTD_did_fieldSize[4] = {0, 1, 2, 4};

#define ZSTD_BLOCKHEADERSIZE 3  
static const size_t ZSTD_blockHeaderSize = ZSTD_BLOCKHEADERSIZE;
typedef enum { bt_raw, bt_rle, bt_compressed, bt_reserved } blockType_e;

#define MIN_SEQUENCES_SIZE 1									   
#define MIN_CBLOCK_SIZE (1   + 1   + MIN_SEQUENCES_SIZE  )  

#define HufLog 12
typedef enum { set_basic, set_rle, set_compressed, set_repeat } symbolEncodingType_e;

#define LONGNBSEQ 0x7F00

#define MINMATCH 3
#define EQUAL_READ32 4

#define Litbits 8
#define MaxLit ((1 << Litbits) - 1)
#define MaxML 52
#define MaxLL 35
#define MaxOff 28
#define MaxSeq MAX(MaxLL, MaxML)  
#define MLFSELog 9
#define LLFSELog 9
#define OffFSELog 8

static const U32 LL_bits[MaxLL + 1] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const S16 LL_defaultNorm[MaxLL + 1] = {4, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 3, 2, 1, 1, 1, 1, 1, -1, -1, -1, -1};
#define LL_DEFAULTNORMLOG 6  
static const U32 LL_defaultNormLog = LL_DEFAULTNORMLOG;

static const U32 ML_bits[MaxML + 1] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  0,  0,  0,  0,  0,  0, 0,
				       0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 4, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
static const S16 ML_defaultNorm[MaxML + 1] = {1, 4, 3, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,  1,  1,  1,  1,  1,  1, 1,
					      1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1, -1, -1};
#define ML_DEFAULTNORMLOG 6  
static const U32 ML_defaultNormLog = ML_DEFAULTNORMLOG;

static const S16 OF_defaultNorm[MaxOff + 1] = {1, 1, 1, 1, 1, 1, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, -1, -1, -1, -1, -1};
#define OF_DEFAULTNORMLOG 5  
static const U32 OF_defaultNormLog = OF_DEFAULTNORMLOG;

 


ZSTD_STATIC void ZSTD_copy8(void *dst, const void *src) {
	memcpy(dst, src, 8);
}
 

#define WILDCOPY_OVERLENGTH 8
ZSTD_STATIC void ZSTD_wildcopy(void *dst, const void *src, ptrdiff_t length)
{
	const BYTE* ip = (const BYTE*)src;
	BYTE* op = (BYTE*)dst;
	BYTE* const oend = op + length;
	 




	if (length <= 8)
		return ZSTD_copy8(dst, src);
	do {
		ZSTD_copy8(op, ip);
		op += 8;
		ip += 8;
	} while (op < oend);
}

 

ZSTD_STATIC U32 ZSTD_highbit32(U32 val) { return 31 - __builtin_clz(val); }


#endif  
