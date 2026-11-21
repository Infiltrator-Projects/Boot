/*
 * Huffman table construction types and error-reporting interface.
 * Oversubscribed or incomplete length arrays must not create out-of-range
 * decode tables for the compressed EFI image path.
 */
#ifndef INFTREES_H
#define INFTREES_H

 




 




 







/*
 * bits counts code bits consumed by this lookup. op directs the decoder to a
 * literal, a base value with extra bits, a subtable, or an end/error
 * condition. val is interpreted according to that operation.
 */
typedef struct {
    unsigned char op;            
    unsigned char bits;          
    unsigned short val;          
} code;

 







 




#define ENOUGH 2048
#define MAXD 592

 
typedef enum {
    CODES,
    LENS,
    DISTS
} codetype;

extern int zlib_inflate_table (codetype type, unsigned short *lens,
                             unsigned codes, code **table,
                             unsigned *bits, unsigned short *work);
#endif
