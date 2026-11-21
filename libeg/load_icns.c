/*
 * Apple ICNS icon decoding and run-length expansion. Container element
 * sizes and channel counts originate from image data and constrain decoding.
 * Failure to find a suitable variant is cosmetic, not an OS detection fault.
 */
 



































#include "libegint.h"

#define MAX_ICNS_SIZES 4

 
 
 

/*
 * Expands ICNS run-length channel data within the caller's pixel count;
 * compressed input size and output capacity are separate bounds.
 */
VOID egDecompressIcnsRLE(IN OUT UINT8 **CompData, IN OUT UINTN *CompLen, IN UINT8 *PixelData, IN UINTN PixelCount)
{
    UINT8 *cp;
    UINT8 *cp_end;
    UINT8 *pp;
    UINTN pp_left;
    UINTN len, i;
    UINT8 value;

     
    cp = *CompData;
    cp_end = cp + *CompLen;
    pp = PixelData;
    pp_left = PixelCount;

     
    while (cp + 1 < cp_end && pp_left > 0) {
        len = *cp++;
        if (len & 0x80) {    
            len -= 125;
            if (len > pp_left)
                break;
            value = *cp++;
            for (i = 0; i < len; i++) {
                *pp = value;
                pp += 4;
            }
        } else {             
            len++;
            if (len > pp_left || cp + len > cp_end)
                break;
            for (i = 0; i < len; i++) {
                *pp = *cp++;
                pp += 4;
            }
        }
        pp_left -= len;
    }

    if (pp_left > 0) {
        Print(L" egDecompressIcnsRLE: still need %d bytes of pixel data\n", pp_left);
    }

     
    *CompData = cp;
    *CompLen = (UINTN)(cp_end - cp);
}

 
 
 

/*
 * Selects and decodes a compatible embedded icon representation. Parser
 * errors must not propagate as a false EFI loader or filesystem failure.
 */
EG_IMAGE * egDecodeICNS(IN UINT8 *FileData, IN UINTN FileDataLength, IN UINTN IconSize, IN BOOLEAN WantAlpha)
{
    EG_IMAGE            *NewImage;
    UINT8               *Ptr, *BufferEnd, *DataPtr, *MaskPtr;
    UINT32              BlockLen, DataLen, MaskLen;
    UINTN               PixelCount, i;
    UINT8               *CompData;
    UINTN               CompLen;
    UINT8               *SrcPtr;
    EG_PIXEL            *DestPtr;
    UINTN               SizesToTry[MAX_ICNS_SIZES + 1] = {IconSize, 128, 48, 32, 16};
    UINTN               SizeToTry = 0;

    if (FileDataLength < 8 || FileData == NULL ||
        FileData[0] != 'i' || FileData[1] != 'c' || FileData[2] != 'n' || FileData[3] != 's') {
         
        return NULL;
    }

    for (;;) {
        DataPtr = NULL;
        DataLen = 0;
        MaskPtr = NULL;
        MaskLen = 0;

        do {
           IconSize = SizesToTry[SizeToTry];
           Ptr = FileData + 8;
           BufferEnd = FileData + FileDataLength;
            
           while (Ptr + 8 <= BufferEnd) {
               BlockLen = ((UINT32)Ptr[4] << 24) + ((UINT32)Ptr[5] << 16) + ((UINT32)Ptr[6] << 8) + (UINT32)Ptr[7];
               if (Ptr + BlockLen > BufferEnd)    
                   break;

                
               if (IconSize == 128) {
                   if (Ptr[0] == 'i' && Ptr[1] == 't' && Ptr[2] == '3' && Ptr[3] == '2') {
                       if (Ptr[8] == 0 && Ptr[9] == 0 && Ptr[10] == 0 && Ptr[11] == 0) {
                           DataPtr = Ptr + 12;
                           DataLen = BlockLen - 12;
                       }
                   } else if (Ptr[0] == 't' && Ptr[1] == '8' && Ptr[2] == 'm' && Ptr[3] == 'k') {
                       MaskPtr = Ptr + 8;
                       MaskLen = BlockLen - 8;
                   }

               } else if (IconSize == 48) {
                   if (Ptr[0] == 'i' && Ptr[1] == 'h' && Ptr[2] == '3' && Ptr[3] == '2') {
                       DataPtr = Ptr + 8;
                       DataLen = BlockLen - 8;
                   } else if (Ptr[0] == 'h' && Ptr[1] == '8' && Ptr[2] == 'm' && Ptr[3] == 'k') {
                       MaskPtr = Ptr + 8;
                       MaskLen = BlockLen - 8;
                   }

               } else if (IconSize == 32) {
                   if (Ptr[0] == 'i' && Ptr[1] == 'l' && Ptr[2] == '3' && Ptr[3] == '2') {
                       DataPtr = Ptr + 8;
                       DataLen = BlockLen - 8;
                   } else if (Ptr[0] == 'l' && Ptr[1] == '8' && Ptr[2] == 'm' && Ptr[3] == 'k') {
                       MaskPtr = Ptr + 8;
                       MaskLen = BlockLen - 8;
                   }

               } else if (IconSize == 16) {
                   if (Ptr[0] == 'i' && Ptr[1] == 's' && Ptr[2] == '3' && Ptr[3] == '2') {
                       DataPtr = Ptr + 8;
                       DataLen = BlockLen - 8;
                   } else if (Ptr[0] == 's' && Ptr[1] == '8' && Ptr[2] == 'm' && Ptr[3] == 'k') {
                       MaskPtr = Ptr + 8;
                       MaskLen = BlockLen - 8;
                   }

               }

               Ptr += BlockLen;
           }
        } while ((DataPtr == NULL) && (SizeToTry++ < MAX_ICNS_SIZES));

        break;
    }

    if (DataPtr == NULL)
        return NULL;    

     
    NewImage = egCreateImage(IconSize, IconSize, WantAlpha);
    if (NewImage == NULL)
        return NULL;
    PixelCount = IconSize * IconSize;

    if (DataLen < PixelCount * 3) {

         
        CompData = DataPtr;
        CompLen  = DataLen;
        egDecompressIcnsRLE(&CompData, &CompLen, PLPTR(NewImage, r), PixelCount);
        egDecompressIcnsRLE(&CompData, &CompLen, PLPTR(NewImage, g), PixelCount);
        egDecompressIcnsRLE(&CompData, &CompLen, PLPTR(NewImage, b), PixelCount);
         
        if (CompLen > 0) {
            Print(L" egLoadICNSIcon: %d bytes of compressed data left\n", CompLen);
        }

    } else {

         
        SrcPtr  = DataPtr;
        DestPtr = NewImage->PixelData;
        for (i = 0; i < PixelCount; i++, DestPtr++) {
            DestPtr->r = *SrcPtr++;
            DestPtr->g = *SrcPtr++;
            DestPtr->b = *SrcPtr++;
        }

    }

     
    if (MaskPtr != NULL && MaskLen >= PixelCount && WantAlpha)
        egInsertPlane(MaskPtr, PLPTR(NewImage, a), PixelCount);
    else
        egSetPlane(PLPTR(NewImage, a), WantAlpha ? 255 : 0, PixelCount);

     

    return NewImage;
}  

 
