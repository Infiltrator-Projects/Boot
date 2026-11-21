/*
 * GNU-EFI type and firmware ABI adaptation for the FSW reader. Host callback
 * signatures and on-disk data widths must not be inferred from native desktop
 * word size; this layer is compiled under UEFI constraints.
 */
 




 































#ifndef _FSW_EFI_BASE_H_
#define _FSW_EFI_BASE_H_

 
#ifdef __MAKEWITH_TIANO
#include "fsw_efi_edk2_base.h"
#else
 
#include <efi.h>
#include <efilib.h>
#endif

#include "../include/refit_call_wrapper.h"

#define FSW_LITTLE_ENDIAN (1)


 

typedef INT8    fsw_s8;
typedef UINT8   fsw_u8;
typedef INT16   fsw_s16;
typedef UINT16  fsw_u16;
typedef INT32   fsw_s32;
typedef UINT32  fsw_u32;
typedef INT64   fsw_s64;
typedef UINT64  fsw_u64;
/*
 * Translate an EFI pool allocation into both an FSW status and an output
 * pointer. This double-indirection contract is also used by fsw_memdup when a
 * backend copies metadata out of a pinned block.
 */
#define fsw_alloc(size, ptrptr) (((*(ptrptr) = AllocatePool(size)) == NULL) ? FSW_OUT_OF_MEMORY : FSW_SUCCESS)
#define fsw_free(ptr) FreePool(ptr)

 

#define fsw_memzero(dest,size) ZeroMem(dest,size)
#ifdef __MAKEWITH_GNUEFI
#define fsw_memcpy(dest,src,size) refit_call3_wrapper(gBS->CopyMem, dest,src,size)
#else
#define fsw_memcpy(dest,src,size) CopyMem(dest,src,size)
#endif
#define fsw_memeq(p1,p2,size) (CompareMem(p1,p2,size) == 0)

 

#define FSW_MSGSTR(s) L##s
#define FSW_MSGFUNC Print

 

#define FSW_U64_SHR(val,shiftbits) RShiftU64((val), (shiftbits))
#define FSW_U64_DIV(val,divisor) DivU64x32((val), (divisor), NULL)
#ifdef __MAKEWITH_GNUEFI
#define DivU64x32Remainder DivU64x32
#endif

#endif
