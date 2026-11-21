/*
 * ReiserFS FSW backend state and tree-item lookup declarations. Metadata
 * keys and item offsets are on-disk values; tree traversal assumes validated
 * node sizes and must report invalid or unsupported layouts as errors.
 */
 




 

















#ifndef _FSW_REISERFS_H_
#define _FSW_REISERFS_H_

#define VOLSTRUCTNAME fsw_reiserfs_volume
#define DNODESTRUCTNAME fsw_reiserfs_dnode
#include "fsw_core.h"

#include "fsw_reiserfs_disk.h"


 
#define REISERFS_SUPERBLOCK_BLOCKSIZEBITS  12
 
#define REISERFS_SUPERBLOCK_BLOCKSIZE  (1<<REISERFS_SUPERBLOCK_BLOCKSIZEBITS)
/*
 * item_data points into the pinned leaf in block_buffer. path_bno and
 * path_index record the search descent so item_next can reach a successor in
 * a different leaf. Release the pin before discarding or replacing this
 * cursor.
 */
struct fsw_reiserfs_item {
    int valid;
    
     
    struct item_head ih;
    fsw_u64 item_offset;
    fsw_u32 item_type;
    
    fsw_u8 *item_data;
    
     
    fsw_u32 path_bno[MAX_HEIGHT];
    fsw_u32 path_index[MAX_HEIGHT];
    
     
    fsw_u32 block_bno;
    void *block_buffer;
};


 



struct fsw_reiserfs_volume {
    struct fsw_volume g;             
    
    struct reiserfs_super_block *sb;   
    int version;                     
};

 



struct fsw_reiserfs_dnode {
    struct fsw_dnode g;              
    
    fsw_u32 dir_id;                  
    struct stat_data_v1 *sd_v1;      
    struct stat_data *sd_v2;         
};


#endif
