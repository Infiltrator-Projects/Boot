 




 

















#ifndef _FSW_EXT2_H_
#define _FSW_EXT2_H_

#define VOLSTRUCTNAME fsw_ext2_volume
#define DNODESTRUCTNAME fsw_ext2_dnode
#include "fsw_core.h"

#include "fsw_ext2_disk.h"


 
#define EXT2_SUPERBLOCK_BLOCKSIZE  1024
 
#define EXT2_SUPERBLOCK_BLOCKNO       1


 



struct fsw_ext2_volume {
    struct fsw_volume g;             
    
    struct ext2_super_block *sb;     
    fsw_u32     *inotab_bno;         
    fsw_u32     ind_bcnt;            
    fsw_u32     dind_bcnt;           
    fsw_u32     inode_size;          
};

 



struct fsw_ext2_dnode {
    struct fsw_dnode g;              
    
    struct ext2_inode *raw;          
};


#endif
