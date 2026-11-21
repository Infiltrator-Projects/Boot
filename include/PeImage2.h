/*
 * GNU-EFI image loader context describing loaded PE/COFF sections and
 * relocations. Image size, entry point and relocation regions require bounds
 * and address-width validation before firmware execution.
 */

#ifndef _PEIMAGE2_H_
#define _PEIMAGE2_H_
/*
 * The shim interface receives this context across a function-pointer call.
 * The section and directory fields are parsed pointers, so copying the
 * context alone does not make an independent copy of the underlying image
 * headers.
 */
typedef struct _GNUEFI_PE_COFF_LOADER_IMAGE_CONTEXT {
   UINT64                            ImageAddress;
   UINT64                            ImageSize;
   UINT64                            EntryPoint; 
   UINTN                             SizeOfHeaders;
   UINT16                            ImageType;
   UINT16                            NumberOfSections;
   EFI_IMAGE_SECTION_HEADER          *FirstSection;
   EFI_IMAGE_DATA_DIRECTORY          *RelocDir;
   EFI_IMAGE_DATA_DIRECTORY          *SecDir;
   UINT64                            NumberOfRvaAndSizes;
   EFI_IMAGE_OPTIONAL_HEADER_UNION   *PEHdr;
} GNUEFI_PE_COFF_LOADER_IMAGE_CONTEXT;

#endif
