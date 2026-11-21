#ifndef INFLATE_H
#define INFLATE_H

 




 




#include "inftrees.h"

 
typedef enum {
    HEAD,        
    FLAGS,       
    TIME,        
    OS,          
    EXLEN,       
    EXTRA,       
    NAME,        
    COMMENT,     
    HCRC,        
    DICTID,      
    DICT,        
        TYPE,        
        TYPEDO,      
        STORED,      
        COPY,        
        TABLE,       
        LENLENS,     
        CODELENS,    
            LEN,         
            LENEXT,      
            DIST,        
            DISTEXT,     
            MATCH,       
            LIT,         
    CHECK,       
    LENGTH,      
    DONE,        
    BAD,         
    MEM,         
    SYNC         
} inflate_mode;

 






















 
struct inflate_state {
    inflate_mode mode;           
    int last;                    
    int wrap;                    
    int havedict;                
    int flags;                   
    unsigned dmax;               
    unsigned long check;         
    unsigned long total;         
              
         
    unsigned wbits;              
    unsigned wsize;              
    unsigned whave;              
    unsigned write;              
    unsigned char *window;   
         
    unsigned long hold;          
    unsigned bits;               
         
    unsigned length;             
    unsigned offset;             
         
    unsigned extra;              
         
    code const *lencode;     
    code const *distcode;    
    unsigned lenbits;            
    unsigned distbits;           
         
    unsigned ncode;              
    unsigned nlen;               
    unsigned ndist;              
    unsigned have;               
    code *next;              
    unsigned short lens[320];    
    unsigned short work[288];    
    code codes[ENOUGH];          
};

 
#define REVERSE(q) \
    ((((q) >> 24) & 0xff) + (((q) >> 8) & 0xff00) + \
     (((q) & 0xff00) << 8) + (((q) & 0xff) << 24))

#endif
