/*
 * ReiserFS disk-format structures and key/layout constants. Packed fields
 * mirror filesystem data blocks, not aligned host structs; do not silently
 * alter sizes, endian handling or key ordering. Directory/tree bounds are
 * security-sensitive when parsing a boot volume.
 */
 




 


















#ifndef _FSW_REISERFS_DISK_H_
#define _FSW_REISERFS_DISK_H_

 

typedef fsw_s8  __s8;
typedef fsw_u8  __u8;
typedef fsw_s16 __s16;
typedef fsw_u16 __u16;
typedef fsw_s32 __s32;
typedef fsw_u32 __u32;
typedef fsw_s64 __s64;
typedef fsw_u64 __u64;

typedef __u16   __le16;
typedef __u32   __le32;
typedef __u64   __le64;

#define le16_to_cpu(x) (x)
#define cpu_to_le16(x) (x)
#define le32_to_cpu(x) (x)
#define cpu_to_le32(x) (x)
#define le64_to_cpu(x) (x)
#define cpu_to_le64(x) (x)

#ifdef __GCC__
#define ATTR_PACKED __attribute__ ((__packed__))
#else
#define ATTR_PACKED
#endif

#pragma pack(1)

 
 
 



 



 
 
 

 



#define UNSET_HASH 0		 
		      
#define TEA_HASH  1
#define YURA_HASH 2
#define R5_HASH   3
#define DEFAULT_HASH R5_HASH

struct journal_params {
	__le32 jp_journal_1st_block;	 

	__le32 jp_journal_dev;	 
	__le32 jp_journal_size;	 
	__le32 jp_journal_trans_max;	 
	__le32 jp_journal_magic;	 

	__le32 jp_journal_max_batch;	 

	__le32 jp_journal_max_commit_age;	 

	__le32 jp_journal_max_trans_age;	 

};

 
struct reiserfs_super_block_v1 {
	__le32 s_block_count;	 
	__le32 s_free_blocks;	 
	__le32 s_root_block;	 
	struct journal_params s_journal;
	__le16 s_blocksize;	 
	__le16 s_oid_maxsize;	 

	__le16 s_oid_cursize;	 
	__le16 s_umount_state;	 

	char s_magic[10];	 


	__le16 s_fs_state;	 

	__le32 s_hash_function_code;	 

	__le16 s_tree_height;	 
	__le16 s_bmap_nr;	 

	__le16 s_version;	 

	__le16 s_reserved_for_journal;	 


} ATTR_PACKED;

#define SB_SIZE_V1 (sizeof(struct reiserfs_super_block_v1))

 
struct reiserfs_super_block {
	struct reiserfs_super_block_v1 s_v1;
	__le32 s_inode_generation;
	__le32 s_flags;		 
	unsigned char s_uuid[16];	 
	unsigned char s_label[16];	 
	char s_unused[88];	 



} ATTR_PACKED;

#define SB_SIZE (sizeof(struct reiserfs_super_block))

#define REISERFS_VERSION_1 0
#define REISERFS_VERSION_2 2

 
#define SB_DISK_SUPER_BLOCK(s) (REISERFS_SB(s)->s_rs)
#define SB_V1_DISK_SUPER_BLOCK(s) (&(SB_DISK_SUPER_BLOCK(s)->s_v1))
#define SB_BLOCKSIZE(s) \
        le32_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_blocksize))
#define SB_BLOCK_COUNT(s) \
        le32_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_block_count))
#define SB_FREE_BLOCKS(s) \
        le32_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_free_blocks))
#define SB_REISERFS_MAGIC(s) \
        (SB_V1_DISK_SUPER_BLOCK(s)->s_magic)
#define SB_ROOT_BLOCK(s) \
        le32_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_root_block))
#define SB_TREE_HEIGHT(s) \
        le16_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_tree_height))
#define SB_REISERFS_STATE(s) \
        le16_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_umount_state))
#define SB_VERSION(s) le16_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_version))
#define SB_BMAP_NR(s) le16_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_bmap_nr))

#define SB_ONDISK_JP(s) (&SB_V1_DISK_SUPER_BLOCK(s)->s_journal)
#define SB_ONDISK_JOURNAL_SIZE(s) \
         le32_to_cpu ((SB_ONDISK_JP(s)->jp_journal_size))
#define SB_ONDISK_JOURNAL_1st_BLOCK(s) \
         le32_to_cpu ((SB_ONDISK_JP(s)->jp_journal_1st_block))
#define SB_ONDISK_JOURNAL_DEVICE(s) \
         le32_to_cpu ((SB_ONDISK_JP(s)->jp_journal_dev))
#define SB_ONDISK_RESERVED_FOR_JOURNAL(s) \
         le16_to_cpu ((SB_V1_DISK_SUPER_BLOCK(s)->s_reserved_for_journal))

#define is_block_in_log_or_reserved_area(s, block) \
         block >= SB_JOURNAL_1st_RESERVED_BLOCK(s) \
         && block < SB_JOURNAL_1st_RESERVED_BLOCK(s) +  \
         ((!is_reiserfs_jr(SB_DISK_SUPER_BLOCK(s)) ? \
         SB_ONDISK_JOURNAL_SIZE(s) + 1 : SB_ONDISK_RESERVED_FOR_JOURNAL(s)))

				 
#define REISERFS_SUPER_MAGIC 0x52654973
				 

#define REISERFS_SUPER_MAGIC_STRING "ReIsErFs"
#define REISER2FS_SUPER_MAGIC_STRING "ReIsEr2Fs"
#define REISER2FS_JR_SUPER_MAGIC_STRING "ReIsEr3Fs"

 




#define REISERFS_DISK_OFFSET_IN_BYTES (64 * 1024)
#define REISERFS_FIRST_BLOCK unused_define
#define REISERFS_JOURNAL_OFFSET_IN_BYTES REISERFS_DISK_OFFSET_IN_BYTES

 
#define REISERFS_OLD_DISK_OFFSET_IN_BYTES (8 * 1024)

 
#define CARRY_ON      0
#define REPEAT_SEARCH -1
#define IO_ERROR      -2
#define NO_DISK_SPACE -3
#define NO_BALANCING_NEEDED  (-4)
#define NO_MORE_UNUSED_CONTIGUOUS_BLOCKS (-5)
#define QUOTA_EXCEEDED -6

typedef __u32 b_blocknr_t;
typedef __le32 unp_t;

struct unfm_nodeinfo {
	unp_t unfm_nodenum;
	unsigned short unfm_freespace;
};
/*
 * The key version determines how the item offset and type are encoded.
 * Old-format keys carry a separate uniqueness value. The newer on-disk format
 * packs the type into the high bits of the offset word.
 */
#define KEY_FORMAT_3_5 0
#define KEY_FORMAT_3_6 1

 
#define STAT_DATA_V1 0
#define STAT_DATA_V2 1



 

#define get_inode_item_key_version( inode )                                    \
    ((REISERFS_I(inode)->i_flags & i_item_key_version_mask) ? KEY_FORMAT_3_6 : KEY_FORMAT_3_5)

#define set_inode_item_key_version( inode, version )                           \
         ({ if((version)==KEY_FORMAT_3_6)                                      \
                REISERFS_I(inode)->i_flags |= i_item_key_version_mask;      \
            else                                                               \
                REISERFS_I(inode)->i_flags &= ~i_item_key_version_mask; })

#define get_inode_sd_version(inode)                                            \
    ((REISERFS_I(inode)->i_flags & i_stat_data_version_mask) ? STAT_DATA_V2 : STAT_DATA_V1)

#define set_inode_sd_version(inode, version)                                   \
         ({ if((version)==STAT_DATA_V2)                                        \
                REISERFS_I(inode)->i_flags |= i_stat_data_version_mask;     \
            else                                                               \
                REISERFS_I(inode)->i_flags &= ~i_stat_data_version_mask; })

 









#define STORE_TAIL_IN_UNFM_S1(n_file_size,n_tail_size,n_block_size) \
(\
  (!(n_tail_size)) || \
  (((n_tail_size) > MAX_DIRECT_ITEM_LEN(n_block_size)) || \
   ( (n_file_size) >= (n_block_size) * 4 ) || \
   ( ( (n_file_size) >= (n_block_size) * 3 ) && \
     ( (n_tail_size) >=   (MAX_DIRECT_ITEM_LEN(n_block_size))/4) ) || \
   ( ( (n_file_size) >= (n_block_size) * 2 ) && \
     ( (n_tail_size) >=   (MAX_DIRECT_ITEM_LEN(n_block_size))/2) ) || \
   ( ( (n_file_size) >= (n_block_size) ) && \
     ( (n_tail_size) >=   (MAX_DIRECT_ITEM_LEN(n_block_size) * 3)/4) ) ) \
)

 




#define STORE_TAIL_IN_UNFM_S2(n_file_size,n_tail_size,n_block_size) \
(\
  (!(n_tail_size)) || \
  (((n_file_size) > MAX_DIRECT_ITEM_LEN(n_block_size)) ) \
)

 


#define REISERFS_VALID_FS    1
#define REISERFS_ERROR_FS    2

 
 
 
#define TYPE_STAT_DATA 0
#define TYPE_INDIRECT 1
#define TYPE_DIRECT 2
#define TYPE_DIRENTRY 3
#define TYPE_MAXTYPE 3
#define TYPE_ANY 15		 

 
 
 

 
 
 
struct offset_v1 {
	__le32 k_offset;
	__le32 k_uniqueness;
} ATTR_PACKED;

struct offset_v2 {
	__le64 v;
} ATTR_PACKED;

 












 

struct reiserfs_key {
	__le32 k_dir_id;	 

	__le32 k_objectid;	 
	union {
		struct offset_v1 k_offset_v1;
		struct offset_v2 k_offset_v2;
	} ATTR_PACKED u;
} ATTR_PACKED;

struct in_core_key {
	__u32 k_dir_id;		 

	__u32 k_objectid;	 
	__u64 k_offset;
	__u8 k_type;
};

struct cpu_key {
	struct in_core_key on_disk_key;
	int version;
	int key_length;		 

};

 



#define REISERFS_FULL_KEY_LEN     4
#define REISERFS_SHORT_KEY_LEN    2

 
#define FIRST_GREATER 1
#define SECOND_GREATER -1
#define KEYS_IDENTICAL 0
#define KEY_FOUND 1
#define KEY_NOT_FOUND 0

#define KEY_SIZE (sizeof(struct reiserfs_key))
#define SHORT_KEY_SIZE (sizeof (__u32) + sizeof (__u32))

 
#define ITEM_FOUND 1
#define ITEM_NOT_FOUND 0
#define ENTRY_FOUND 1
#define ENTRY_NOT_FOUND 0
#define DIRECTORY_NOT_FOUND -1
#define REGULAR_FILE_FOUND -2
#define DIRECTORY_FOUND -3
#define BYTE_FOUND 1
#define BYTE_NOT_FOUND 0
#define FILE_NOT_FOUND -1

#define POSITION_FOUND 1
#define POSITION_NOT_FOUND 0

 
#define NAME_FOUND 1
#define NAME_NOT_FOUND 0
#define GOTO_PREVIOUS_ITEM 2
#define NAME_FOUND_INVISIBLE 3

 




struct item_head {
	 

	struct reiserfs_key ih_key;
	union {
		 





		__le16 ih_free_space_reserved;
		 

		__le16 ih_entry_count;
	} ATTR_PACKED u;
	__le16 ih_item_len;	 
	__le16 ih_item_location;	 

	__le16 ih_version;	 



} ATTR_PACKED;
 
#define IH_SIZE (sizeof(struct item_head))

#define ih_free_space(ih)            le16_to_cpu((ih)->u.ih_free_space_reserved)
#define ih_version(ih)               le16_to_cpu((ih)->ih_version)
#define ih_entry_count(ih)           le16_to_cpu((ih)->u.ih_entry_count)
#define ih_location(ih)              le16_to_cpu((ih)->ih_item_location)
#define ih_item_len(ih)              le16_to_cpu((ih)->ih_item_len)

#define unreachable_item(ih) (ih_version(ih) & (1 << 15))

#define get_ih_free_space(ih) (ih_version (ih) == KEY_FORMAT_3_6 ? 0 : ih_free_space (ih))

 





#define get_block_num(p, i) le32_to_cpu(get_unaligned((p) + (i)))

 
 
 
#define V1_SD_UNIQUENESS 0
#define V1_INDIRECT_UNIQUENESS 0xfffffffe
#define V1_DIRECT_UNIQUENESS 0xffffffff
#define V1_DIRENTRY_UNIQUENESS 500
#define V1_ANY_UNIQUENESS 555	 

 
 
 
 






































 
 
 
 
 
 


























#define is_direntry_le_key(version,key) (le_key_k_type (version, key) == TYPE_DIRENTRY)
#define is_direct_le_key(version,key) (le_key_k_type (version, key) == TYPE_DIRECT)
#define is_indirect_le_key(version,key) (le_key_k_type (version, key) == TYPE_INDIRECT)
#define is_statdata_le_key(version,key) (le_key_k_type (version, key) == TYPE_STAT_DATA)

 
 
 
#define is_direntry_le_ih(ih) is_direntry_le_key (ih_version (ih), &((ih)->ih_key))
#define is_direct_le_ih(ih) is_direct_le_key (ih_version (ih), &((ih)->ih_key))
#define is_indirect_le_ih(ih) is_indirect_le_key (ih_version(ih), &((ih)->ih_key))
#define is_statdata_le_ih(ih) is_statdata_le_key (ih_version (ih), &((ih)->ih_key))

 
 
 
 
















#define is_direntry_cpu_key(key) (cpu_key_k_type (key) == TYPE_DIRENTRY)
#define is_direct_cpu_key(key) (cpu_key_k_type (key) == TYPE_DIRECT)
#define is_indirect_cpu_key(key) (cpu_key_k_type (key) == TYPE_INDIRECT)
#define is_statdata_cpu_key(key) (cpu_key_k_type (key) == TYPE_STAT_DATA)

 
#define is_direntry_cpu_ih(ih) (is_direntry_cpu_key (&((ih)->ih_key)))
#define is_direct_cpu_ih(ih) (is_direct_cpu_key (&((ih)->ih_key)))
#define is_indirect_cpu_ih(ih) (is_indirect_cpu_key (&((ih)->ih_key)))
#define is_statdata_cpu_ih(ih) (is_statdata_cpu_key (&((ih)->ih_key)))

#define I_K_KEY_IN_ITEM(p_s_ih, p_s_key, n_blocksize) \
    ( ! COMP_SHORT_KEYS(p_s_ih, p_s_key) && \
          I_OFF_BYTE_IN_ITEM(p_s_ih, k_offset (p_s_key), n_blocksize) )

 
#define MAX_ITEM_LEN(block_size) (block_size - BLKH_SIZE - IH_SIZE)
#define MIN_ITEM_LEN 1

 
#define REISERFS_ROOT_OBJECTID 2
#define REISERFS_ROOT_PARENT_OBJECTID 1

 








 

struct block_head {
	__le16 blk_level;	 
	__le16 blk_nr_item;	 
	__le16 blk_free_space;	 
	__le16 blk_reserved;
	 
	struct reiserfs_key blk_right_delim_key;	 
};

#define BLKH_SIZE                     (sizeof(struct block_head))
#define blkh_level(p_blkh)            (le16_to_cpu((p_blkh)->blk_level))
#define blkh_nr_item(p_blkh)          (le16_to_cpu((p_blkh)->blk_nr_item))
#define blkh_free_space(p_blkh)       (le16_to_cpu((p_blkh)->blk_free_space))
#define blkh_reserved(p_blkh)         (le16_to_cpu((p_blkh)->blk_reserved))
#define blkh_right_delim_key(p_blkh)  ((p_blkh)->blk_right_delim_key)

 



#define FREE_LEVEL 0		 




#define DISK_LEAF_NODE_LEVEL  1	 

 
#define B_BLK_HEAD(p_s_bh)            ((struct block_head *)((p_s_bh)->b_data))
 
#define B_NR_ITEMS(p_s_bh)            (blkh_nr_item(B_BLK_HEAD(p_s_bh)))
#define B_LEVEL(p_s_bh)               (blkh_level(B_BLK_HEAD(p_s_bh)))
#define B_FREE_SPACE(p_s_bh)          (blkh_free_space(B_BLK_HEAD(p_s_bh)))

 
#define B_PRIGHT_DELIM_KEY(p_s_bh)   (&(blk_right_delim_key(B_BLK_HEAD(p_s_bh))

 
#define B_IS_ITEMS_LEVEL(p_s_bh)     (B_LEVEL(p_s_bh) == DISK_LEAF_NODE_LEVEL)

 
#define B_IS_KEYS_LEVEL(p_s_bh)      (B_LEVEL(p_s_bh) > DISK_LEAF_NODE_LEVEL \
                                            && B_LEVEL(p_s_bh) <= MAX_HEIGHT)

 
 
 

 
 
 
 
struct stat_data_v1 {
	__le16 sd_mode;		 
	__le16 sd_nlink;	 
	__le16 sd_uid;		 
	__le16 sd_gid;		 
	__le32 sd_size;		 
	__le32 sd_atime;	 
	__le32 sd_mtime;	 
	__le32 sd_ctime;	 
	union {
		__le32 sd_rdev;
		__le32 sd_blocks;	 
	} ATTR_PACKED u;
	__le32 sd_first_direct_byte;	 








} ATTR_PACKED;

#define SD_V1_SIZE              (sizeof(struct stat_data_v1))
#define stat_data_v1(ih)        (ih_version (ih) == KEY_FORMAT_3_5)
#define sd_v1_mode(sdp)         (le16_to_cpu((sdp)->sd_mode))
#define set_sd_v1_mode(sdp,v)   ((sdp)->sd_mode = cpu_to_le16(v))
#define sd_v1_nlink(sdp)        (le16_to_cpu((sdp)->sd_nlink))
#define set_sd_v1_nlink(sdp,v)  ((sdp)->sd_nlink = cpu_to_le16(v))
#define sd_v1_uid(sdp)          (le16_to_cpu((sdp)->sd_uid))
#define set_sd_v1_uid(sdp,v)    ((sdp)->sd_uid = cpu_to_le16(v))
#define sd_v1_gid(sdp)          (le16_to_cpu((sdp)->sd_gid))
#define set_sd_v1_gid(sdp,v)    ((sdp)->sd_gid = cpu_to_le16(v))
#define sd_v1_size(sdp)         (le32_to_cpu((sdp)->sd_size))
#define set_sd_v1_size(sdp,v)   ((sdp)->sd_size = cpu_to_le32(v))
#define sd_v1_atime(sdp)        (le32_to_cpu((sdp)->sd_atime))
#define set_sd_v1_atime(sdp,v)  ((sdp)->sd_atime = cpu_to_le32(v))
#define sd_v1_mtime(sdp)        (le32_to_cpu((sdp)->sd_mtime))
#define set_sd_v1_mtime(sdp,v)  ((sdp)->sd_mtime = cpu_to_le32(v))
#define sd_v1_ctime(sdp)        (le32_to_cpu((sdp)->sd_ctime))
#define set_sd_v1_ctime(sdp,v)  ((sdp)->sd_ctime = cpu_to_le32(v))
#define sd_v1_rdev(sdp)         (le32_to_cpu((sdp)->u.sd_rdev))
#define set_sd_v1_rdev(sdp,v)   ((sdp)->u.sd_rdev = cpu_to_le32(v))
#define sd_v1_blocks(sdp)       (le32_to_cpu((sdp)->u.sd_blocks))
#define set_sd_v1_blocks(sdp,v) ((sdp)->u.sd_blocks = cpu_to_le32(v))
#define sd_v1_first_direct_byte(sdp) \
                                (le32_to_cpu((sdp)->sd_first_direct_byte))
#define set_sd_v1_first_direct_byte(sdp,v) \
                                ((sdp)->sd_first_direct_byte = cpu_to_le32(v))

 



 

 

#define REISERFS_IMMUTABLE_FL EXT2_IMMUTABLE_FL
#define REISERFS_APPEND_FL    EXT2_APPEND_FL
#define REISERFS_SYNC_FL      EXT2_SYNC_FL
#define REISERFS_NOATIME_FL   EXT2_NOATIME_FL
#define REISERFS_NODUMP_FL    EXT2_NODUMP_FL
#define REISERFS_SECRM_FL     EXT2_SECRM_FL
#define REISERFS_UNRM_FL      EXT2_UNRM_FL
#define REISERFS_COMPR_FL     EXT2_COMPR_FL
#define REISERFS_NOTAIL_FL    EXT2_NOTAIL_FL

 
#define REISERFS_INHERIT_MASK ( REISERFS_IMMUTABLE_FL |	\
				REISERFS_SYNC_FL |	\
				REISERFS_NOATIME_FL |	\
				REISERFS_NODUMP_FL |	\
				REISERFS_SECRM_FL |	\
				REISERFS_COMPR_FL |	\
				REISERFS_NOTAIL_FL )

 

struct stat_data {
	__le16 sd_mode;		 
	__le16 sd_attrs;	 
	__le32 sd_nlink;	 
	__le64 sd_size;		 
	__le32 sd_uid;		 
	__le32 sd_gid;		 
	__le32 sd_atime;	 
	__le32 sd_mtime;	 
	__le32 sd_ctime;	 
	__le32 sd_blocks;
	union {
		__le32 sd_rdev;
		__le32 sd_generation;
		 
		 







	} ATTR_PACKED u;
} ATTR_PACKED;
 
 
 
#define SD_SIZE (sizeof(struct stat_data))
#define SD_V2_SIZE              SD_SIZE
#define stat_data_v2(ih)        (ih_version (ih) == KEY_FORMAT_3_6)
#define sd_v2_mode(sdp)         (le16_to_cpu((sdp)->sd_mode))
#define set_sd_v2_mode(sdp,v)   ((sdp)->sd_mode = cpu_to_le16(v))
 
 
#define sd_v2_nlink(sdp)        (le32_to_cpu((sdp)->sd_nlink))
#define set_sd_v2_nlink(sdp,v)  ((sdp)->sd_nlink = cpu_to_le32(v))
#define sd_v2_size(sdp)         (le64_to_cpu((sdp)->sd_size))
#define set_sd_v2_size(sdp,v)   ((sdp)->sd_size = cpu_to_le64(v))
#define sd_v2_uid(sdp)          (le32_to_cpu((sdp)->sd_uid))
#define set_sd_v2_uid(sdp,v)    ((sdp)->sd_uid = cpu_to_le32(v))
#define sd_v2_gid(sdp)          (le32_to_cpu((sdp)->sd_gid))
#define set_sd_v2_gid(sdp,v)    ((sdp)->sd_gid = cpu_to_le32(v))
#define sd_v2_atime(sdp)        (le32_to_cpu((sdp)->sd_atime))
#define set_sd_v2_atime(sdp,v)  ((sdp)->sd_atime = cpu_to_le32(v))
#define sd_v2_mtime(sdp)        (le32_to_cpu((sdp)->sd_mtime))
#define set_sd_v2_mtime(sdp,v)  ((sdp)->sd_mtime = cpu_to_le32(v))
#define sd_v2_ctime(sdp)        (le32_to_cpu((sdp)->sd_ctime))
#define set_sd_v2_ctime(sdp,v)  ((sdp)->sd_ctime = cpu_to_le32(v))
#define sd_v2_blocks(sdp)       (le32_to_cpu((sdp)->sd_blocks))
#define set_sd_v2_blocks(sdp,v) ((sdp)->sd_blocks = cpu_to_le32(v))
#define sd_v2_rdev(sdp)         (le32_to_cpu((sdp)->u.sd_rdev))
#define set_sd_v2_rdev(sdp,v)   ((sdp)->u.sd_rdev = cpu_to_le32(v))
#define sd_v2_generation(sdp)   (le32_to_cpu((sdp)->u.sd_generation))
#define set_sd_v2_generation(sdp,v) ((sdp)->u.sd_generation = cpu_to_le32(v))
#define sd_v2_attrs(sdp)         (le16_to_cpu((sdp)->sd_attrs))
#define set_sd_v2_attrs(sdp,v)   ((sdp)->sd_attrs = cpu_to_le16(v))

 
 
 
 












#define SD_OFFSET  0
#define SD_UNIQUENESS 0
#define DOT_OFFSET 1
#define DOT_DOT_OFFSET 2
#define DIRENTRY_UNIQUENESS 500

 
#define FIRST_ITEM_OFFSET 1

 





 


struct reiserfs_de_head {
	__le32 deh_offset;	 
	__le32 deh_dir_id;	 

	__le32 deh_objectid;	 
	__le16 deh_location;	 
	__le16 deh_state;	 

} ATTR_PACKED;
#define DEH_SIZE                  sizeof(struct reiserfs_de_head)
#define deh_offset(p_deh)         (le32_to_cpu((p_deh)->deh_offset))
#define deh_dir_id(p_deh)         (le32_to_cpu((p_deh)->deh_dir_id))
#define deh_objectid(p_deh)       (le32_to_cpu((p_deh)->deh_objectid))
#define deh_location(p_deh)       (le16_to_cpu((p_deh)->deh_location))
#define deh_state(p_deh)          (le16_to_cpu((p_deh)->deh_state))

#define put_deh_offset(p_deh,v)   ((p_deh)->deh_offset = cpu_to_le32((v)))
#define put_deh_dir_id(p_deh,v)   ((p_deh)->deh_dir_id = cpu_to_le32((v)))
#define put_deh_objectid(p_deh,v) ((p_deh)->deh_objectid = cpu_to_le32((v)))
#define put_deh_location(p_deh,v) ((p_deh)->deh_location = cpu_to_le16((v)))
#define put_deh_state(p_deh,v)    ((p_deh)->deh_state = cpu_to_le16((v)))

 
#define EMPTY_DIR_SIZE \
(DEH_SIZE * 2 + ROUND_UP (strlen (".")) + ROUND_UP (strlen ("..")))

 
#define EMPTY_DIR_SIZE_V1 (DEH_SIZE * 2 + 3)

#define DEH_Statdata 0		 
#define DEH_Visible 2

 
#if BITS_PER_LONG == 64 || defined(__s390__) || defined(__hppa__)
#   define ADDR_UNALIGNED_BITS  (3)
#endif

 


#ifdef ADDR_UNALIGNED_BITS

#   define aligned_address(addr)           ((void *)((long)(addr) & ~((1UL << ADDR_UNALIGNED_BITS) - 1)))
#   define unaligned_offset(addr)          (((int)((long)(addr) & ((1 << ADDR_UNALIGNED_BITS) - 1))) << 3)

#   define set_bit_unaligned(nr, addr)     ext2_set_bit((nr) + unaligned_offset(addr), aligned_address(addr))
#   define clear_bit_unaligned(nr, addr)   ext2_clear_bit((nr) + unaligned_offset(addr), aligned_address(addr))
#   define test_bit_unaligned(nr, addr)    ext2_test_bit((nr) + unaligned_offset(addr), aligned_address(addr))

#else

#   define set_bit_unaligned(nr, addr)     ext2_set_bit(nr, addr)
#   define clear_bit_unaligned(nr, addr)   ext2_clear_bit(nr, addr)
#   define test_bit_unaligned(nr, addr)    ext2_test_bit(nr, addr)

#endif

#define mark_de_with_sd(deh)        set_bit_unaligned (DEH_Statdata, &((deh)->deh_state))
#define mark_de_without_sd(deh)     clear_bit_unaligned (DEH_Statdata, &((deh)->deh_state))
#define mark_de_visible(deh)	    set_bit_unaligned (DEH_Visible, &((deh)->deh_state))
#define mark_de_hidden(deh)	    clear_bit_unaligned (DEH_Visible, &((deh)->deh_state))

#define de_with_sd(deh)		    test_bit_unaligned (DEH_Statdata, &((deh)->deh_state))
#define de_visible(deh)	    	    test_bit_unaligned (DEH_Visible, &((deh)->deh_state))
#define de_hidden(deh)	    	    !test_bit_unaligned (DEH_Visible, &((deh)->deh_state))

 






 
  
#define B_I_PITEM(bh,ih) ( (bh)->b_data + ih_location(ih) )
#define B_I_DEH(bh,ih) ((struct reiserfs_de_head *)(B_I_PITEM(bh,ih)))

 





 



 













 
#define I_ENTRY_COUNT(ih) (ih_entry_count((ih)))

 
#define B_I_E_NAME(bh,ih,entry_num) ((char *)(bh->b_data + ih_location(ih) + deh_location(B_I_DEH(bh,ih)+(entry_num))))

 
#define REISERFS_MAX_NAME(block_size) 255

 

 

struct reiserfs_dir_entry {
	struct buffer_head *de_bh;
	int de_item_num;
	struct item_head *de_ih;
	int de_entry_num;
	struct reiserfs_de_head *de_deh;
	int de_entrylen;
	int de_namelen;
	char *de_name;
	unsigned long *de_gen_number_bit_string;

	__u32 de_dir_id;
	__u32 de_objectid;

	struct cpu_key de_entry_key;
};

 

 
#define B_I_DEH_ENTRY_FILE_NAME(bh,ih,deh) (B_I_PITEM (bh, ih) + deh_location(deh))

 
#define I_DEH_N_ENTRY_FILE_NAME_LENGTH(ih,deh,entry_num) \
(I_DEH_N_ENTRY_LENGTH (ih, deh, entry_num) - (de_with_sd (deh) ? SD_SIZE : 0))

 
#define GET_HASH_VALUE(offset) ((offset) & 0x7fffff80LL)
 
#define GET_GENERATION_NUMBER(offset) ((offset) & 0x7fLL)
#define MAX_GENERATION_NUMBER  127

#define SET_GENERATION_NUMBER(offset,gen_number) (GET_HASH_VALUE(offset)|(gen_number))

 








 
 
 
 

struct disk_child {
	__le32 dc_block_number;	 
	__le16 dc_size;		 
	__le16 dc_reserved;
};

#define DC_SIZE (sizeof(struct disk_child))
#define dc_block_number(dc_p)	(le32_to_cpu((dc_p)->dc_block_number))
#define dc_size(dc_p)		(le16_to_cpu((dc_p)->dc_size))

 
#define B_N_CHILD(p_s_bh,n_pos)  ((struct disk_child *)\
((p_s_bh)->b_data+BLKH_SIZE+B_NR_ITEMS(p_s_bh)*KEY_SIZE+DC_SIZE*(n_pos)))

 
#define B_N_CHILD_NUM(p_s_bh,n_pos) (dc_block_number(B_N_CHILD(p_s_bh,n_pos)))
#define PUT_B_N_CHILD_NUM(p_s_bh,n_pos, val) (put_dc_block_number(B_N_CHILD(p_s_bh,n_pos), val ))

  
  
#define MAX_CHILD_SIZE(bh) ((int)( (bh)->b_size - BLKH_SIZE ))

 
#define B_CHILD_SIZE(cur) (MAX_CHILD_SIZE(cur)-(B_FREE_SPACE(cur)))

 
#define MAX_NR_KEY(bh) ( (MAX_CHILD_SIZE(bh)-DC_SIZE)/(KEY_SIZE+DC_SIZE) )
#define MIN_NR_KEY(bh)    (MAX_NR_KEY(bh)/2)

 
 
 

 







struct path_element {
	struct buffer_head *pe_buffer;	 
	int pe_position;	 
	 
};

#define MAX_HEIGHT 5		 
#define EXTENDED_MAX_HEIGHT         7	 
#define FIRST_PATH_ELEMENT_OFFSET   2	 

#define ILLEGAL_PATH_ELEMENT_OFFSET 1	 
#define MAX_FEB_SIZE 6		 

 
















#define PATH_READA	0x1	 
#define PATH_READA_BACK 0x2	 

struct path {
	int path_length;	 
	int reada;
	struct path_element path_elements[EXTENDED_MAX_HEIGHT];	 
	int pos_in_item;
};

#define pos_in_item(path) ((path)->pos_in_item)

#define INITIALIZE_PATH(var) \
struct path var = {.path_length = ILLEGAL_PATH_ELEMENT_OFFSET, .reada = 0,}

 
#define PATH_OFFSET_PELEMENT(p_s_path,n_offset)  ((p_s_path)->path_elements +(n_offset))

 
#define PATH_OFFSET_PBUFFER(p_s_path,n_offset)   (PATH_OFFSET_PELEMENT(p_s_path,n_offset)->pe_buffer)

 
#define PATH_OFFSET_POSITION(p_s_path,n_offset) (PATH_OFFSET_PELEMENT(p_s_path,n_offset)->pe_position)

#define PATH_PLAST_BUFFER(p_s_path) (PATH_OFFSET_PBUFFER((p_s_path), (p_s_path)->path_length))
				 





#define PATH_LAST_POSITION(p_s_path) (PATH_OFFSET_POSITION((p_s_path), (p_s_path)->path_length))

#define PATH_PITEM_HEAD(p_s_path)    B_N_PITEM_HEAD(PATH_PLAST_BUFFER(p_s_path),PATH_LAST_POSITION(p_s_path))

 

#define PATH_H_PBUFFER(p_s_path, h) PATH_OFFSET_PBUFFER (p_s_path, p_s_path->path_length - (h))	 
#define PATH_H_PPARENT(path, h) PATH_H_PBUFFER (path, (h) + 1)	 
#define PATH_H_POSITION(path, h) PATH_OFFSET_POSITION (path, path->path_length - (h))
#define PATH_H_B_ITEM_ORDER(path, h) PATH_H_POSITION(path, h + 1)	 

#define PATH_H_PATH_OFFSET(p_s_path, n_h) ((p_s_path)->path_length - (n_h))

#define get_last_bh(path) PATH_PLAST_BUFFER(path)
#define get_ih(path) PATH_PITEM_HEAD(path)
#define get_item_pos(path) PATH_LAST_POSITION(path)
#define get_item(path) ((void *)B_N_PITEM(PATH_PLAST_BUFFER(path), PATH_LAST_POSITION (path)))
#define item_moved(ih,path) comp_items(ih, path)
#define path_changed(ih,path) comp_items (ih, path)

 
 
 

 
#define UNFM_P_SIZE (sizeof(unp_t))
#define UNFM_P_SHIFT 2

 
#define INODE_PKEY(inode) ((struct reiserfs_key *)(REISERFS_I(inode)->i_key))

#define MAX_UL_INT 0xffffffff
#define MAX_INT    0x7ffffff
#define MAX_US_INT 0xffff

 
#define U32_MAX (~(__u32)0)

 









 
#define MAX_KEY_OBJECTID	MAX_UL_INT

#define MAX_B_NUM  MAX_UL_INT
#define MAX_FC_NUM MAX_US_INT

 
#define REISERFS_LINK_MAX (MAX_US_INT - 1000)

 
#define REISERFS_KERNEL_MEM		0	 
#define REISERFS_USER_MEM		1	 

#define fs_generation(s) (REISERFS_SB(s)->s_generation_counter)
#define get_generation(s) atomic_read (&fs_generation(s))
#define FILESYSTEM_CHANGED_TB(tb)  (get_generation((tb)->tb_sb) != (tb)->fs_gen)
#define __fs_changed(gen,s) (gen != get_generation (s))
#define fs_changed(gen,s) ({cond_resched(); __fs_changed(gen, s);})

 
 
 

#define VI_TYPE_LEFT_MERGEABLE 1
#define VI_TYPE_RIGHT_MERGEABLE 2

 












struct virtual_item {
	int vi_index;		 
	unsigned short vi_type;	 
	unsigned short vi_item_len;	 
	struct item_head *vi_ih;
	const char *vi_item;	 
	const void *vi_new_data;	 
	void *vi_uarea;		 
};

struct virtual_node {
	char *vn_free_ptr;	 
	unsigned short vn_nr_item;	 
	short vn_size;		 
	short vn_mode;		 
	short vn_affected_item_num;
	short vn_pos_in_item;
	struct item_head *vn_ins_ih;	 
	const void *vn_data;
	struct virtual_item *vn_vi;	 
};

 
struct direntry_uarea {
	int flags;
	__u16 entry_count;
	__u16 entry_sizes[1];
} ATTR_PACKED;

 
 
 

 










#define MAX_FREE_BLOCK 7	 

 
#define MAX_AMOUNT_NEEDED 2

 
struct tree_balance {
	int tb_mode;
	int need_balance_dirty;
	struct super_block *tb_sb;
	struct reiserfs_transaction_handle *transaction_handle;
	struct path *tb_path;
	struct buffer_head *L[MAX_HEIGHT];	 
	struct buffer_head *R[MAX_HEIGHT];	 
	struct buffer_head *FL[MAX_HEIGHT];	 
	struct buffer_head *FR[MAX_HEIGHT];	 
	struct buffer_head *CFL[MAX_HEIGHT];	 
	struct buffer_head *CFR[MAX_HEIGHT];	 

	struct buffer_head *FEB[MAX_FEB_SIZE];	 

	struct buffer_head *used[MAX_FEB_SIZE];
	struct buffer_head *thrown[MAX_FEB_SIZE];
	int lnum[MAX_HEIGHT];	 








	int rnum[MAX_HEIGHT];	 
	int lkey[MAX_HEIGHT];	 

	int rkey[MAX_HEIGHT];	 
	int insert_size[MAX_HEIGHT];	 

	int blknum[MAX_HEIGHT];	 





	 
	int cur_blknum;		 
	int s0num;		 
	int s1num;		 
	int s2num;		 
	int lbytes;		 
	 
	 
	int rbytes;		 
	 
	 
	int s1bytes;		 
	 
	int s2bytes;
	struct buffer_head *buf_to_free[MAX_FREE_BLOCK];	 
	char *vn_buf;		 


	int vn_buf_size;	 
	struct virtual_node *tb_vn;	 

	int fs_gen;		 

#ifdef DISPLACE_NEW_PACKING_LOCALITIES
	struct in_core_key key;	 

#endif
};

 

 
#define M_INSERT	'i'
 

#define M_PASTE		'p'
 
#define M_DELETE	'd'
 
#define M_CUT 		'c'

 
#define M_INTERNAL	'n'

 

#define M_SKIP_BALANCING 		's'
#define M_CONVERT	'v'

 
#define LEAF_FROM_S_TO_L 0
#define LEAF_FROM_S_TO_R 1
#define LEAF_FROM_R_TO_L 2
#define LEAF_FROM_L_TO_R 3
#define LEAF_FROM_S_TO_SNEW 4

#define FIRST_TO_LAST 0
#define LAST_TO_FIRST 1

 

struct buffer_info {
	struct tree_balance *tb;
	struct buffer_head *bi_bh;
	struct buffer_head *bi_parent;
	int bi_position;
};

 















struct item_operations {
	int (*bytes_number) (struct item_head * ih, int block_size);
	void (*decrement_key) (struct cpu_key *);
	int (*is_left_mergeable) (struct reiserfs_key * ih,
				  unsigned long bsize);
	void (*print_item) (struct item_head *, char *item);
	void (*check_item) (struct item_head *, char *item);

	int (*create_vi) (struct virtual_node * vn, struct virtual_item * vi,
			  int is_affected, int insert_size);
	int (*check_left) (struct virtual_item * vi, int free,
			   int start_skip, int end_skip);
	int (*check_right) (struct virtual_item * vi, int free);
	int (*part_size) (struct virtual_item * vi, int from, int to);
	int (*unit_num) (struct virtual_item * vi);
	void (*print_vi) (struct virtual_item * vi);
};

extern struct item_operations *item_ops[TYPE_ANY + 1];

#define op_bytes_number(ih,bsize)                    item_ops[le_ih_k_type (ih)]->bytes_number (ih, bsize)
#define op_is_left_mergeable(key,bsize)              item_ops[le_key_k_type (le_key_version (key), key)]->is_left_mergeable (key, bsize)
#define op_print_item(ih,item)                       item_ops[le_ih_k_type (ih)]->print_item (ih, item)
#define op_check_item(ih,item)                       item_ops[le_ih_k_type (ih)]->check_item (ih, item)
#define op_create_vi(vn,vi,is_affected,insert_size)  item_ops[le_ih_k_type ((vi)->vi_ih)]->create_vi (vn,vi,is_affected,insert_size)
#define op_check_left(vi,free,start_skip,end_skip) item_ops[(vi)->vi_index]->check_left (vi, free, start_skip, end_skip)
#define op_check_right(vi,free)                      item_ops[(vi)->vi_index]->check_right (vi, free)
#define op_part_size(vi,from,to)                     item_ops[(vi)->vi_index]->part_size (vi, from, to)
#define op_unit_num(vi)				     item_ops[(vi)->vi_index]->unit_num (vi)
#define op_print_vi(vi)                              item_ops[(vi)->vi_index]->print_vi (vi)

#define COMP_SHORT_KEYS comp_short_keys

 
#define I_UNFM_NUM(p_s_ih)	( ih_item_len(p_s_ih) / UNFM_P_SIZE )

 
#define I_POS_UNFM_SIZE(ih,pos,size) (((pos) == I_UNFM_NUM(ih) - 1 ) ? (size) - ih_free_space(ih) : (size))

 

 
#define B_N_PITEM_HEAD(bh,item_num) ( (struct item_head * )((bh)->b_data + BLKH_SIZE) + (item_num) )

 
#define B_N_PDELIM_KEY(bh,item_num) ( (struct reiserfs_key * )((bh)->b_data + BLKH_SIZE) + (item_num) )

 
#define B_N_PKEY(bh,item_num) ( &(B_N_PITEM_HEAD(bh,item_num)->ih_key) )

 
#define B_N_PITEM(bh,item_num) ( (bh)->b_data + ih_location(B_N_PITEM_HEAD((bh),(item_num))))

 
#define B_N_STAT_DATA(bh,nr) \
( (struct stat_data *)((bh)->b_data + ih_location(B_N_PITEM_HEAD((bh),(nr))) ) )

     

 
#define B_I_STAT_DATA(bh, ih) ( (struct stat_data * )((bh)->b_data + ih_location(ih)) )

 
#define MAX_DIRECT_ITEM_LEN(size) ((size) - BLKH_SIZE - 2*IH_SIZE - SD_SIZE - UNFM_P_SIZE)

 


#define B_I_POS_UNFM_POINTER(bh,ih,pos) le32_to_cpu(*(((unp_t *)B_I_PITEM(bh,ih)) + (pos)))
#define PUT_B_I_POS_UNFM_POINTER(bh,ih,pos, val) do {*(((unp_t *)B_I_PITEM(bh,ih)) + (pos)) = cpu_to_le32(val); } while (0)

struct reiserfs_iget_args {
	__u32 objectid;
	__u32 dirid;
};

 
 
 

 
#define get_journal_desc_magic(bh) (bh->b_data + bh->b_size - 12)

#define journal_trans_half(blocksize) \
	((blocksize - sizeof (struct reiserfs_journal_desc) + sizeof (__u32) - 12) / sizeof (__u32))

 

 
struct reiserfs_journal_desc {
	__le32 j_trans_id;	 
	__le32 j_len;		 
	__le32 j_mount_id;	 
	__le32 j_realblock[1];	 
};

#define get_desc_trans_id(d)   le32_to_cpu((d)->j_trans_id)
#define get_desc_trans_len(d)  le32_to_cpu((d)->j_len)
#define get_desc_mount_id(d)   le32_to_cpu((d)->j_mount_id)

#define set_desc_trans_id(d,val)       do { (d)->j_trans_id = cpu_to_le32 (val); } while (0)
#define set_desc_trans_len(d,val)      do { (d)->j_len = cpu_to_le32 (val); } while (0)
#define set_desc_mount_id(d,val)       do { (d)->j_mount_id = cpu_to_le32 (val); } while (0)

 
struct reiserfs_journal_commit {
	__le32 j_trans_id;	 
	__le32 j_len;		 
	__le32 j_realblock[1];	 
};

#define get_commit_trans_id(c) le32_to_cpu((c)->j_trans_id)
#define get_commit_trans_len(c)        le32_to_cpu((c)->j_len)
#define get_commit_mount_id(c) le32_to_cpu((c)->j_mount_id)

#define set_commit_trans_id(c,val)     do { (c)->j_trans_id = cpu_to_le32 (val); } while (0)
#define set_commit_trans_len(c,val)    do { (c)->j_len = cpu_to_le32 (val); } while (0)

 



struct reiserfs_journal_header {
	__le32 j_last_flush_trans_id;	 
	__le32 j_first_unflushed_offset;	 
	__le32 j_mount_id;
	  struct journal_params jh_journal;
};

 
#define JOURNAL_BLOCK_COUNT 8192	 
#define JOURNAL_TRANS_MAX_DEFAULT 1024	 
#define JOURNAL_TRANS_MIN_DEFAULT 256
#define JOURNAL_MAX_BATCH_DEFAULT   900	 
#define JOURNAL_MIN_RATIO 2
#define JOURNAL_MAX_COMMIT_AGE 30
#define JOURNAL_MAX_TRANS_AGE 30
#define JOURNAL_PER_BALANCE_CNT (3 * (MAX_HEIGHT-2) + 9)
#ifdef CONFIG_QUOTA
 
#define REISERFS_QUOTA_TRANS_BLOCKS(s) (REISERFS_SB(s)->s_mount_opt & (1<<REISERFS_QUOTA) ? 2 : 0)
 
#define REISERFS_QUOTA_INIT_BLOCKS(s) (REISERFS_SB(s)->s_mount_opt & (1<<REISERFS_QUOTA) ? \
(DQUOT_INIT_ALLOC*(JOURNAL_PER_BALANCE_CNT+2)+DQUOT_INIT_REWRITE+1) : 0)
 
#define REISERFS_QUOTA_DEL_BLOCKS(s) (REISERFS_SB(s)->s_mount_opt & (1<<REISERFS_QUOTA) ? \
(DQUOT_DEL_ALLOC*(JOURNAL_PER_BALANCE_CNT+2)+DQUOT_DEL_REWRITE+1) : 0)
#else
#define REISERFS_QUOTA_TRANS_BLOCKS(s) 0
#define REISERFS_QUOTA_INIT_BLOCKS(s) 0
#define REISERFS_QUOTA_DEL_BLOCKS(s) 0
#endif

 





#define REISERFS_MIN_BITMAP_NODES 10
#define REISERFS_MAX_BITMAP_NODES 100

#define JBH_HASH_SHIFT 13	 
#define JBH_HASH_MASK 8191

#define _jhashfn(sb,block)	\
	(((unsigned long)sb>>L1_CACHE_SHIFT) ^ \
	 (((block)<<(JBH_HASH_SHIFT - 6)) ^ ((block) >> 13) ^ ((block) << (JBH_HASH_SHIFT - 12))))
#define journal_hash(t,sb,block) ((t)[_jhashfn((sb),(block)) & JBH_HASH_MASK])

 
#define journal_find_get_block(s, block) __find_get_block(SB_JOURNAL(s)->j_dev_bd, block, s->s_blocksize)
#define journal_getblk(s, block) __getblk(SB_JOURNAL(s)->j_dev_bd, block, s->s_blocksize)
#define journal_bread(s, block) __bread(SB_JOURNAL(s)->j_dev_bd, block, s->s_blocksize)

 
 
 
 
 
 
 
 
 
 

 














 


struct reiserfs_transaction_handle {
	struct super_block *t_super;	 





	int t_refcount;
	int t_blocks_logged;	 
	int t_blocks_allocated;	 
	unsigned long t_trans_id;	 
	void *t_handle_save;	 
	unsigned displace_new_blocks:1;	 

	 
};

 


struct reiserfs_jh {
	struct reiserfs_journal_list *jl;
	struct buffer_head *bh;
	 
};

 
 
 
 




















#define i_block_size(inode) ((inode)->i_sb->s_blocksize)
#define file_size(inode) ((inode)->i_size)
#define tail_size(inode) (file_size (inode) & (i_block_size (inode) - 1))

#define tail_has_to_be_packed(inode) (have_large_tails ((inode)->i_sb)?\
!STORE_TAIL_IN_UNFM_S1(file_size (inode), tail_size(inode), inode->i_sb->s_blocksize):have_small_tails ((inode)->i_sb)?!STORE_TAIL_IN_UNFM_S2(file_size (inode), tail_size(inode), inode->i_sb->s_blocksize):0 )

 
 
#define GET_BLOCK_NO_CREATE 0	 
#define GET_BLOCK_CREATE 1	 
#define GET_BLOCK_NO_HOLE 2	 
#define GET_BLOCK_READ_DIRECT 4	 
#define GET_BLOCK_NO_IMUX     8	 
#define GET_BLOCK_NO_DANGLE   16	 

 

 

struct __reiserfs_blocknr_hint {
	struct inode *inode;	 
	long block;		 
	struct in_core_key key;
	struct path *path;	 

	struct reiserfs_transaction_handle *th;	 

	b_blocknr_t beg, end;
	b_blocknr_t search_start;	 


	int prealloc_size;	 


	unsigned formatted_node:1;	 

	unsigned preallocate:1;
};

typedef struct __reiserfs_blocknr_hint reiserfs_blocknr_hint_t;

 
__u32 keyed_hash(const signed char *msg, int len);
__u32 yura_hash(const signed char *msg, int len);
__u32 r5_hash(const signed char *msg, int len);

 



#define reiserfs_test_and_set_le_bit   ext2_set_bit
#define reiserfs_test_and_clear_le_bit ext2_clear_bit
#define reiserfs_test_le_bit           ext2_test_bit
#define reiserfs_find_next_zero_le_bit ext2_find_next_zero_bit

 






#define SPARE_SPACE 500

 
#define REISERFS_IOC_UNPACK		_IOW(0xCD,1,long)
 

#define REISERFS_IOC_GETFLAGS		EXT2_IOC_GETFLAGS
#define REISERFS_IOC_SETFLAGS		EXT2_IOC_SETFLAGS
#define REISERFS_IOC_GETVERSION		EXT2_IOC_GETVERSION
#define REISERFS_IOC_SETVERSION		EXT2_IOC_SETVERSION



#pragma pack()


#endif
