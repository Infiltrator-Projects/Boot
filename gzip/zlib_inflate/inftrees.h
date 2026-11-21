#ifndef INFTREES_H
#define INFTREES_H

 




 




 












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
