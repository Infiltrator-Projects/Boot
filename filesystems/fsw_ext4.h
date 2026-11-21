/*
 * EXT4 FSW backend state and extent operations. Inode extent flags determine
 * which block-addressing scheme is active; unsupported superblock feature
 * combinations must fail explicitly before walking filesystem metadata.
 */
 




 


















#ifndef _FSW_EXT4_H_
#define _FSW_EXT4_H_

#define VOLSTRUCTNAME fsw_ext4_volume
#define DNODESTRUCTNAME fsw_ext4_dnode
#include "fsw_core.h"

#include "fsw_ext4_disk.h"


 
#define EXT4_SUPERBLOCK_BLOCKSIZE  1024
 
#define EXT4_SUPERBLOCK_BLOCKNO       1


 



struct fsw_ext4_volume {
    struct fsw_volume g;             
    
    struct ext4_super_block *sb;     
    /*
     * Store the descriptor's combined high and low inode-table block address
     * for each group. The width of this metadata address is independent of
     * the file size advertised by an individual dnode.
     */
    fsw_u64     *inotab_bno;         
    fsw_u32     ind_bcnt;            
    fsw_u32     dind_bcnt;           
    fsw_u32     inode_size;          
};

 



struct fsw_ext4_dnode {
    struct fsw_dnode g;              
    
    struct ext4_inode *raw;          
};


#endif
