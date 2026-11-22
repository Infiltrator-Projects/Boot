/*
 * BOOT / Btrfs read path
 *
 * Btrfs reader with logical-to-physical address translation.
 *
 * Tree traversal, subvolume identity and extent decoding share cache state.
 * Chunk mappings and supported features constrain readable layouts.
 */

#include "fsw_core.h"
#define uint8_t fsw_u8
#define uint16_t fsw_u16
#define uint32_t fsw_u32
#define uint64_t fsw_u64
#define int64_t fsw_s64
#define int32_t fsw_s32

#ifndef DPRINT
#define DPRINT(x...)     
#endif

#define fsw_size_t int
#define fsw_ssize_t int

#define grub_off_t int32_t
#define grub_size_t int32_t
#define grub_ssize_t int32_t
#include "crc32c.c"
#include "gzio.c"
#define MINILZO_CFG_SKIP_LZO_PTR 1
#define MINILZO_CFG_SKIP_LZO_UTIL 1
#define MINILZO_CFG_SKIP_LZO_STRING 1
#define MINILZO_CFG_SKIP_LZO_INIT 1
#define MINILZO_CFG_SKIP_LZO1X_DECOMPRESS 1
#define MINILZO_CFG_SKIP_LZO1X_1_COMPRESS 1
#define MINILZO_CFG_SKIP_LZO_STRING 1
#include "minilzo.c"
#include "scandisk.c"

#define BTRFS_DEFAULT_BLOCK_SIZE 4096
#define GRUB_BTRFS_SIGNATURE "_BHRfS_M"

#define GRUB_BTRFS_LZO_BLOCK_SIZE 4096
#define GRUB_BTRFS_LZO_BLOCK_MAX_CSIZE (GRUB_BTRFS_LZO_BLOCK_SIZE + \
        (GRUB_BTRFS_LZO_BLOCK_SIZE / 16) + 64 + 3)

typedef uint8_t btrfs_checksum_t[0x20];
typedef uint32_t btrfs_uuid_t[4];

struct btrfs_device
{
    uint64_t device_id;
    uint64_t size;
    uint8_t dummy[0x62 - 0x10];
} __attribute__ ((__packed__));

struct btrfs_superblock
{
    btrfs_checksum_t checksum;
    btrfs_uuid_t uuid;
    uint8_t dummy[0x10];
    uint8_t signature[sizeof (GRUB_BTRFS_SIGNATURE) - 1];
    uint64_t generation;
    uint64_t root_tree;
    uint64_t chunk_tree;
    uint8_t dummy2[0x10];
    uint64_t total_bytes;
    uint64_t bytes_used;
    uint64_t root_dir_objectid;
#define BTRFS_MAX_NUM_DEVICES   0x10000
    uint64_t num_devices;
    uint32_t sectorsize;
    uint32_t nodesize;

    uint8_t dummy3[0x31];
    struct btrfs_device this_device;
    char label[0x100];
    uint8_t dummy4[0x100];
    uint8_t bootstrap_mapping[0x800];
} __attribute__ ((__packed__));

struct btrfs_header
{
    btrfs_checksum_t checksum;
    btrfs_uuid_t uuid;
    uint8_t dummy[0x30];
    uint32_t nitems;
    uint8_t level;
} __attribute__ ((__packed__));

struct fsw_btrfs_device_desc
{
    struct fsw_volume * dev;
    uint64_t id;
};

#define RECOVER_CACHE_SIZE 17
struct fsw_btrfs_recover_cache
{
    uint64_t device_id;
    uint64_t offset;
    char *buffer;
    BOOLEAN valid;
};

struct fsw_btrfs_volume
{
    struct fsw_volume g;             

    uint8_t bootstrap_mapping[0x800];
    btrfs_uuid_t uuid;
    uint64_t total_bytes;
    uint64_t bytes_used;
    uint64_t chunk_tree;
    uint64_t root_tree;
    uint64_t top_tree;  
    unsigned num_devices;
    unsigned sectorshift;
    unsigned sectorsize;
    int is_master;
    int rescan_once;

    struct fsw_btrfs_device_desc *devices_attached;
    unsigned n_devices_attached;
    unsigned n_devices_allocated;

    uint64_t extstart;
    uint64_t extend;
    uint64_t extino;
    uint64_t exttree;
    uint32_t extsize;
    struct btrfs_extent_data *extent;
    struct fsw_btrfs_recover_cache *rcache;
};

enum
{
    GRUB_BTRFS_ITEM_TYPE_INODE_ITEM = 0x01,
    GRUB_BTRFS_ITEM_TYPE_INODE_REF = 0x0c,
    GRUB_BTRFS_ITEM_TYPE_DIR_ITEM = 0x54,
    GRUB_BTRFS_ITEM_TYPE_EXTENT_ITEM = 0x6c,
    GRUB_BTRFS_ITEM_TYPE_ROOT_ITEM = 0x84,
    GRUB_BTRFS_ITEM_TYPE_DEVICE = 0xd8,
    GRUB_BTRFS_ITEM_TYPE_CHUNK = 0xe4
};

struct btrfs_key
{
    uint64_t object_id;
    uint8_t type;
    uint64_t offset;
} __attribute__ ((__packed__));

struct btrfs_chunk_item
{
    uint64_t size;
    uint64_t dummy;
    uint64_t stripe_length;
    uint64_t type;
#define GRUB_BTRFS_CHUNK_TYPE_BITS_DONTCARE 0x07
#define GRUB_BTRFS_CHUNK_TYPE_SINGLE        0x00
#define GRUB_BTRFS_CHUNK_TYPE_RAID0         0x08
#define GRUB_BTRFS_CHUNK_TYPE_RAID1         0x10
#define GRUB_BTRFS_CHUNK_TYPE_DUPLICATED    0x20
#define GRUB_BTRFS_CHUNK_TYPE_RAID10        0x40
#define GRUB_BTRFS_CHUNK_TYPE_RAID5         0x80
#define GRUB_BTRFS_CHUNK_TYPE_RAID6         0x100
#define GRUB_BTRFS_CHUNK_TYPE_RAID1C3       0x200
#define GRUB_BTRFS_CHUNK_TYPE_RAID1C4       0x400
    uint8_t dummy2[0xc];
    uint16_t nstripes;
    uint16_t nsubstripes;
#define RAID5_TAG	0x100000
} __attribute__ ((__packed__));

struct btrfs_chunk_stripe
{
    uint64_t device_id;
    uint64_t offset;
    btrfs_uuid_t device_uuid;
} __attribute__ ((__packed__));

struct btrfs_leaf_node
{
    struct btrfs_key key;
    uint32_t offset;
    uint32_t size;
} __attribute__ ((__packed__));

struct btrfs_internal_node
{
    struct btrfs_key key;
    uint64_t addr;
    uint64_t dummy;
} __attribute__ ((__packed__));

struct btrfs_dir_item
{
    struct btrfs_key key;
    uint64_t transid;
    uint16_t m;
    uint16_t n;
#define GRUB_BTRFS_DIR_ITEM_TYPE_REGULAR 1
#define GRUB_BTRFS_DIR_ITEM_TYPE_DIRECTORY 2
#define GRUB_BTRFS_DIR_ITEM_TYPE_SYMLINK 7
    uint8_t type;
    char name[0];
} __attribute__ ((__packed__));

struct fsw_btrfs_leaf_descriptor
{
    unsigned depth;
    unsigned allocated;
    struct
    {
        uint64_t addr;
        unsigned iter;
        unsigned maxiter;
        int leaf;
    } *data;
};

struct btrfs_root_item
{
    uint8_t dummy[0xb0];
    uint64_t tree;
    uint64_t inode;
} __attribute__ ((__packed__));

struct btrfs_time
{
    int64_t sec;
    uint32_t nanosec;
} __attribute__ ((__packed__));

struct btrfs_inode
{
    uint64_t gen_id;
    uint64_t trans_id;
    uint64_t size;
    uint64_t nbytes;
    uint64_t block_group;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint32_t mode;
    uint64_t rdev;
    uint64_t flags;

    uint64_t seq;

    uint64_t reserved[4];
    struct btrfs_time atime;
    struct btrfs_time ctime;
    struct btrfs_time mtime;
    struct btrfs_time otime;
} __attribute__ ((__packed__));

struct fsw_btrfs_dnode {
    struct fsw_dnode g;               
    struct btrfs_inode *raw;     
};

struct btrfs_extent_data
{
    uint64_t dummy;
    uint64_t size;
    uint8_t compression;
    uint8_t encryption;
    uint16_t encoding;
    uint8_t type;
    union
    {
        char inl[0];
        struct
        {
            uint64_t laddr;
            uint64_t compressed_size;
            uint64_t offset;
            uint64_t filled;
        };
    };
} __attribute__ ((__packed__));

#define GRUB_BTRFS_EXTENT_INLINE 0
#define GRUB_BTRFS_EXTENT_REGULAR 1

#define GRUB_BTRFS_COMPRESSION_NONE 0
#define GRUB_BTRFS_COMPRESSION_ZLIB 1
#define GRUB_BTRFS_COMPRESSION_LZO  2
#define GRUB_BTRFS_COMPRESSION_ZSTD  3
#define GRUB_BTRFS_COMPRESSION_MAX  3

#define GRUB_BTRFS_OBJECT_ID_CHUNK 0x100

struct fsw_btrfs_uuid_list {
    struct fsw_btrfs_volume *master;
    struct fsw_btrfs_uuid_list *next;
};

static int uuid_eq(btrfs_uuid_t u1, btrfs_uuid_t u2) {
    return u1[0]==u2[0] && u1[1]==u2[1] && u1[2]==u2[2] && u1[3]==u2[3];
}

static struct fsw_btrfs_uuid_list *master_uuid_list = NULL;

/*
 * Maintain a process-local map from filesystem UUID to one master volume
 * for multidevice discovery. Returning zero means an existing master was
 * found; a newly added volume becomes the master and remains on the list
 * until removed. A failed allocation returns -1 to distinguish exhaustion
 * from both an existing master (0) and successful insertion (1).
 */
static int master_uuid_add(struct fsw_btrfs_volume *vol, struct fsw_btrfs_volume **master_out) {
    struct fsw_btrfs_uuid_list *l;

    for (l = master_uuid_list; l; l=l->next)
        if(uuid_eq(l->master->uuid, vol->uuid)) {
            if(master_out)
                *master_out = l->master;
            return 0;
        }

    l = AllocatePool(sizeof(struct fsw_btrfs_uuid_list));
    if (l == NULL)
        return -1;
    l->master = vol;
    l->next = master_uuid_list;
    master_uuid_list = l;
    return 1;
}

/* Remove one master-volume reference from the global UUID registry.
 * The registry owns its link nodes but not the referenced mounted volume;
 * missing entries are a no-op, and iteration is not synchronised.
 */
static void master_uuid_remove(struct fsw_btrfs_volume *vol) {
    struct fsw_btrfs_uuid_list **lp;

    for (lp = &master_uuid_list; *lp; lp=&(*lp)->next)
        if((*lp)->master == vol) {
            struct fsw_btrfs_uuid_list *n = *lp;
            *lp = n->next;
            FreePool(n);
            break;
        }
}

/**
 * Copy selected little-endian superblock metadata into the live volume.
 * An unsupported sector size leaves sectorshift at zero: this helper
 * does not independently validate the full disk geometry.
 */
static fsw_status_t btrfs_set_superblock_info(struct fsw_btrfs_volume *vol, struct btrfs_superblock *sb)
{
    int i;
    vol->uuid[0] = sb->uuid[0];
    vol->uuid[1] = sb->uuid[1];
    vol->uuid[2] = sb->uuid[2];
    vol->uuid[3] = sb->uuid[3];
    vol->chunk_tree = sb->chunk_tree;
    vol->root_tree = sb->root_tree;
    vol->total_bytes = fsw_u64_le_swap(sb->total_bytes);
    vol->bytes_used = fsw_u64_le_swap(sb->bytes_used);

    vol->sectorshift = 0;
    vol->sectorsize = fsw_u32_le_swap(sb->sectorsize);
    for(i=9; i<20; i++) {
        if((1UL<<i) == vol->sectorsize) {
            vol->sectorshift = i;
            break;
        }
    }
    if(fsw_u64_le_swap(sb->num_devices) > BTRFS_MAX_NUM_DEVICES)
        vol->num_devices = BTRFS_MAX_NUM_DEVICES;
    else
        vol->num_devices = fsw_u64_le_swap(sb->num_devices);
    fsw_memcpy(vol->bootstrap_mapping, sb->bootstrap_mapping, sizeof(vol->bootstrap_mapping));
    return FSW_SUCCESS;
}

static uint64_t superblock_pos[4] = {
    64 / 4,
    64 * 1024 / 4,
    256 * 1048576 / 4,
    1048576ULL * 1048576ULL / 4
};

static fsw_status_t fsw_btrfs_read_logical(struct fsw_btrfs_volume *vol,
        uint64_t addr, void *buf, fsw_size_t size, int rdepth, int cache_level);

/**
 * Read mirror positions in units of 4096 bytes. The first usable superblock
 * supplies a device size for bounding later mirror reads. Among the mirrors
 * actually visited, keep the highest generation. An unreadable mirror or
 * signature mismatch stops the loop, so later copies may never be examined.
 */
static fsw_status_t btrfs_read_superblock (struct fsw_volume *vol, struct btrfs_superblock *sb_out)
{
    unsigned i;
    uint64_t total_blocks = 1024;
    fsw_status_t err = FSW_SUCCESS;

    fsw_set_blocksize(vol, BTRFS_DEFAULT_BLOCK_SIZE, BTRFS_DEFAULT_BLOCK_SIZE);
    for (i = 0; i < 4; i++)
    {
        uint8_t *buffer;
        struct btrfs_superblock *sb;

        if (total_blocks <= superblock_pos[i])
            break;

        err = fsw_block_get(vol, superblock_pos[i], 0, (void **)&buffer);
        if (err) {
            fsw_block_release(vol, superblock_pos[i], buffer);
            break;
        }

        sb = (struct btrfs_superblock *)buffer;
        if (!fsw_memeq (sb->signature, GRUB_BTRFS_SIGNATURE,
                    sizeof (GRUB_BTRFS_SIGNATURE) - 1))
        {
            fsw_block_release(vol, superblock_pos[i], buffer);
            break;
        }
        if (i == 0 || fsw_u64_le_swap (sb->generation) > fsw_u64_le_swap (sb_out->generation))
        {
            fsw_memcpy (sb_out, sb, sizeof (*sb));
            total_blocks = fsw_u64_le_swap (sb->this_device.size) >> 12;
        }
        fsw_block_release(vol, superblock_pos[i], buffer);
    }

    if ((err == FSW_UNSUPPORTED || !err) && i == 0)
        return FSW_UNSUPPORTED;

    if (err == FSW_UNSUPPORTED)
        err = FSW_SUCCESS;

    if(err == 0)
        DPRINT(L"btrfs: UUID: %08x-%08x-%08x-%08x device id: %d\n",
                sb_out->uuid[0], sb_out->uuid[1], sb_out->uuid[2], sb_out->uuid[3],
                sb_out->this_device.device_id);
    return err;
}

/**
 * Compare object ID, item type and offset in that order, converting the two
 * multibyte fields from disk order. This ordering keeps an inode's keys adjacent
 * while separating directory items, directory indices and extents. Matching only
 * the numeric offset would mix independent records.
 */
static int key_cmp (const struct btrfs_key *a, const struct btrfs_key *b)
{
    if (fsw_u64_le_swap (a->object_id) < fsw_u64_le_swap (b->object_id))
        return -1;
    if (fsw_u64_le_swap (a->object_id) > fsw_u64_le_swap (b->object_id))
        return +1;

    if (a->type < b->type)
        return -1;
    if (a->type > b->type)
        return +1;

    if (fsw_u64_le_swap (a->offset) < fsw_u64_le_swap (b->offset))
        return -1;
    if (fsw_u64_le_swap (a->offset) > fsw_u64_le_swap (b->offset))
        return +1;
    return 0;
}

/**
 * Release the iterator's traversal stack. The descriptor and its
 * volume are borrowed and are not freed by this helper.
 */
static void free_iterator (struct fsw_btrfs_leaf_descriptor *desc)
{
    fsw_free (desc->data);
}

/* Push a B-tree descent frame recording node address and item cursor.
 * Capacity doubles when the stack fills. Depth increments before allocation;
 * a failed expansion leaves the descriptor depth updated, so the caller
 * must not resume iteration as if the push completed successfully.
 */
static fsw_status_t save_ref (struct fsw_btrfs_leaf_descriptor *desc,
        uint64_t addr, unsigned i, unsigned m, int l)
{
    desc->depth++;
    if (desc->allocated < desc->depth)
    {
        void *newdata;
        int oldsize = sizeof (desc->data[0]) * desc->allocated;
        desc->allocated *= 2;
        newdata = AllocatePool (sizeof (desc->data[0]) * desc->allocated);
        if (!newdata)
            return FSW_OUT_OF_MEMORY;
        fsw_memcpy(newdata, desc->data, oldsize);
        FreePool(desc->data);
        desc->data = newdata;
    }
    desc->data[desc->depth - 1].addr = addr;
    desc->data[desc->depth - 1].iter = i;
    desc->data[desc->depth - 1].maxiter = m;
    desc->data[desc->depth - 1].leaf = l;
    return FSW_SUCCESS;
}

/* Advance the saved B-tree walk to the next leaf item. Return zero when
 * exhausted, one with a borrowed logical item address/size and key on success,
 * or a negative FSW error. Internal-node descent consumes and extends the
 * descriptor stack; it requires a successfully maintained save_ref state.
 */
static int next (struct fsw_btrfs_volume *vol,
        struct fsw_btrfs_leaf_descriptor *desc,
        uint64_t * outaddr, fsw_size_t * outsize,
        struct btrfs_key *key_out)
{
    fsw_status_t err;
    struct btrfs_leaf_node leaf;

    for (; desc->depth > 0; desc->depth--)
    {
        desc->data[desc->depth - 1].iter++;
        if (desc->data[desc->depth - 1].iter
                < desc->data[desc->depth - 1].maxiter)
            break;
    }
    if (desc->depth == 0)
        return 0;
    while (!desc->data[desc->depth - 1].leaf)
    {
        struct btrfs_internal_node node;
        struct btrfs_header head;
        fsw_memzero(&node, sizeof(node));

        err = fsw_btrfs_read_logical (vol, desc->data[desc->depth - 1].iter
                * sizeof (node)
                + sizeof (struct btrfs_header)
                + desc->data[desc->depth - 1].addr,
                &node, sizeof (node), 0, 1);
        if (err)
            return -err;

        err = fsw_btrfs_read_logical (vol, fsw_u64_le_swap (node.addr),
                &head, sizeof (head), 0, 1);
        if (err)
            return -err;

        save_ref (desc, fsw_u64_le_swap (node.addr), 0,
                fsw_u32_le_swap (head.nitems), !head.level);
    }
    err = fsw_btrfs_read_logical (vol, desc->data[desc->depth - 1].iter
            * sizeof (leaf)
            + sizeof (struct btrfs_header)
            + desc->data[desc->depth - 1].addr, &leaf,
            sizeof (leaf), 0, 1);
    if (err)
        return -err;
    *outsize = fsw_u32_le_swap (leaf.size);
    *outaddr = desc->data[desc->depth - 1].addr + sizeof (struct btrfs_header)
        + fsw_u32_le_swap (leaf.offset);
    *key_out = leaf.key;
    return 1;
}

#define depth2cache(x)  ((x) >= 4 ? 1 : 5-(x))
/**
 * Find the greatest key less than or equal to the request. Internal nodes retain
 * the last eligible separator, and leaves return an exact match or its
 * predecessor. A request below the first key can return success with a zero key,
 * size and address. Exact-lookup callers must compare key_out.
 *
 * Records within each visited node are scanned linearly. The optional descriptor
 * saves node addresses and slot indices for subsequent iteration. rdepth bounds
 * recursive logical-address translation during a tree read and is separate from
 * descent through the tree's levels.
 */
static fsw_status_t lower_bound (struct fsw_btrfs_volume *vol,
        const struct btrfs_key *key_in,
        struct btrfs_key *key_out,
        uint64_t root,
        uint64_t *outaddr, fsw_size_t *outsize,
        struct fsw_btrfs_leaf_descriptor *desc,
        int rdepth)
{
    uint64_t addr = fsw_u64_le_swap (root);
    int depth = -1;

    if (desc)
    {
        desc->allocated = 16;
        desc->depth = 0;
        desc->data = AllocatePool (sizeof (desc->data[0]) * desc->allocated);
        if (!desc->data)
            return FSW_OUT_OF_MEMORY;
    }

    if (rdepth > 10)
        return FSW_VOLUME_CORRUPTED;

    DPRINT (L"btrfs: retrieving %lx %x %lx\n",
            key_in->object_id, key_in->type, key_in->offset);

    while (1)
    {
        fsw_status_t err;
        struct btrfs_header head;
        fsw_memzero(&head, sizeof(head));

reiter:
        depth++;

        err = fsw_btrfs_read_logical (vol, addr, &head, sizeof (head),
                rdepth + 1, depth2cache(rdepth));
        if (err)
            return err;
        addr += sizeof (head);
        if (head.level)
        {
            unsigned i;
            struct btrfs_internal_node node, node_last;
            int have_last = 0;
            fsw_memzero (&node_last, sizeof (node_last));
            for (i = 0; i < fsw_u32_le_swap (head.nitems); i++)
            {
                err = fsw_btrfs_read_logical (vol, addr + i * sizeof (node),
                        &node, sizeof (node), rdepth + 1, depth2cache(rdepth));
                if (err)
                    return err;

                DPRINT (L"btrfs: internal node (depth %d) %lx %x %lx\n", depth,
                        node.key.object_id, node.key.type,
                        node.key.offset);

                if (key_cmp (&node.key, key_in) == 0)
                {
                    err = FSW_SUCCESS;
                    if (desc)
                        err = save_ref (desc, addr - sizeof (head), i,
                                fsw_u32_le_swap (head.nitems), 0);
                    if (err)
                        return err;
                    addr = fsw_u64_le_swap (node.addr);
                    goto reiter;
                }
                if (key_cmp (&node.key, key_in) > 0)
                    break;
                node_last = node;
                have_last = 1;
            }
            if (have_last)
            {
                err = FSW_SUCCESS;
                if (desc)
                    err = save_ref (desc, addr - sizeof (head), i - 1,
                            fsw_u32_le_swap (head.nitems), 0);
                if (err)
                    return err;
                addr = fsw_u64_le_swap (node_last.addr);
                goto reiter;
            }
            *outsize = 0;
            *outaddr = 0;
            fsw_memzero (key_out, sizeof (*key_out));
            if (desc)
                return save_ref (desc, addr - sizeof (head), -1,
                        fsw_u32_le_swap (head.nitems), 0);
            return FSW_SUCCESS;
        }
        {
            unsigned i;
            struct btrfs_leaf_node leaf, leaf_last;
            int have_last = 0;
            for (i = 0; i < fsw_u32_le_swap (head.nitems); i++)
            {
                err = fsw_btrfs_read_logical (vol, addr + i * sizeof (leaf),
                        &leaf, sizeof (leaf), rdepth + 1, depth2cache(rdepth));
                if (err)
                    return err;

                DPRINT (L"btrfs: leaf (depth %d) %lx %x %lx\n", depth,
                        leaf.key.object_id, leaf.key.type, leaf.key.offset);

                if (key_cmp (&leaf.key, key_in) == 0)
                {
                    fsw_memcpy (key_out, &leaf.key, sizeof (*key_out));
                    *outsize = fsw_u32_le_swap (leaf.size);
                    *outaddr = addr + fsw_u32_le_swap (leaf.offset);
                    if (desc)
                        return save_ref (desc, addr - sizeof (head), i,
                                fsw_u32_le_swap (head.nitems), 1);
                    return FSW_SUCCESS;
                }

                if (key_cmp (&leaf.key, key_in) > 0)
                    break;

                have_last = 1;
                leaf_last = leaf;
            }

            if (have_last)
            {
                fsw_memcpy (key_out, &leaf_last.key, sizeof (*key_out));
                *outsize = fsw_u32_le_swap (leaf_last.size);
                *outaddr = addr + fsw_u32_le_swap (leaf_last.offset);
                if (desc)
                    return save_ref (desc, addr - sizeof (head), i - 1,
                            fsw_u32_le_swap (head.nitems), 1);
                return FSW_SUCCESS;
            }
            *outsize = 0;
            *outaddr = 0;
            fsw_memzero (key_out, sizeof (*key_out));
            if (desc)
                return save_ref (desc, addr - sizeof (head), -1,
                        fsw_u32_le_swap (head.nitems), 1);
            return FSW_SUCCESS;
        }
    }
}

/**
 * Reject duplicate member IDs, then transfer a cloned physical member
 * into the master's table. Caller scan_disks_hook must already check both
 * UUID equality and array capacity; neither is revalidated here.
 */
static int btrfs_add_multi_device(struct fsw_btrfs_volume *master, struct fsw_volume *slave, struct btrfs_superblock *sb)
{
    int i;
    for( i = 0; i < master->n_devices_attached; i++)
        if(sb->this_device.device_id == master->devices_attached[i].id)
            return FSW_UNSUPPORTED;

    slave = clone_dummy_volume(slave);
    if(slave == NULL)
            return FSW_OUT_OF_MEMORY;
    fsw_set_blocksize(slave, master->sectorsize, master->sectorsize);

    master->devices_attached[i].id = sb->this_device.device_id;
    master->devices_attached[i].dev = slave;
    master->n_devices_attached++;

    DPRINT(L"Found slave %d\n", sb->this_device.device_id);
    return FSW_SUCCESS;
}

/*
 * Attempt to attach one candidate disk to a multidevice Btrfs mount.
 * Capacity is bounded by n_devices_allocated, and a readable superblock
 * must carry the same filesystem UUID before btrfs_add_multi_device
 * accepts the member. A matching UUID is a grouping condition, not a
 * guarantee of available stripes or consistent data.
 */
static int scan_disks_hook(struct fsw_volume *volg, struct fsw_volume *slave) {
    struct fsw_btrfs_volume *vol = (struct fsw_btrfs_volume *)volg;
    struct btrfs_superblock sb;
    fsw_status_t err;
    btrfs_uuid_t u;

    if(vol->n_devices_attached >= vol->n_devices_allocated)
        return FSW_UNSUPPORTED;

    err = btrfs_read_superblock(slave, &sb);
    if(err)
        return FSW_UNSUPPORTED;

    u[0] = sb.uuid[0];
    u[1] = sb.uuid[1];
    u[2] = sb.uuid[2];
    u[3] = sb.uuid[3];
    if(!uuid_eq(vol->uuid, u))
        return FSW_UNSUPPORTED;

    return btrfs_add_multi_device(vol, slave, &sb);
}

/**
 * Clear the rescan flag before enumerating devices. Enumeration can itself lead
 * back into the filesystem, so consuming the single attempt first prevents the
 * same missing member from triggering recursive rescans.
 */
static int do_rescan_once(struct fsw_btrfs_volume *vol) {
    if(vol->rescan_once == 0 || vol->n_devices_attached >= vol->n_devices_allocated)
	return 0;
    vol->rescan_once = 0;
    return scan_disks(scan_disks_hook, &vol->g);
}

static struct fsw_volume *
/* Lookup a Btrfs member by its on-disk device identifier among attached
 * devices. The returned fsw_volume is borrowed from the multi-device mount;
 * absence is not proof that the missing member cannot later be discovered.
 */
find_device (struct fsw_btrfs_volume *vol, uint64_t id) {
    int i;

    for (i = 0; i < vol->n_devices_attached; i++)
	if (id == vol->devices_attached[i].id)
	    return vol->devices_attached[i].dev;
    DPRINT(L"sub device %d not found\n", id);
    return NULL;
}

struct stripe_table {
    struct fsw_volume *dev;
    uint64_t off;
    char *ptr;
};

/**
 * XOR one RAID stripe into another using machine-width words.
 * The caller must provide aligned buffers and a byte count divisible
 * by sizeof(UINTN); the primitive does not check those assumptions.
 */
static void block_xor(char *dst, const char *src, uint32_t blocksize)
{
    UINTN *d = (UINTN *)dst;
    const UINTN *s = (const UINTN *)src;
    blocksize /= sizeof(UINTN);
    uint32_t i;
    for( i = 0; i < blocksize; i++)
	d[i] ^= s[i];
}

/**
 * Reconstruct XOR parity across present stripe pointers, including
 * index data_stripes. NULL stripe pointers contribute zero. Array
 * capacity, pointer alignment and readable byte counts are caller duties.
 */
static void stripe_xor(char *dst, struct stripe_table *stripe, int data_stripes, uint32_t blocksize)
{
    unsigned i, j;
    UINTN c;
    for(j = 0; j < blocksize; j += sizeof(UINTN)) {

	for(c=0, i=0; i <= data_stripes; i++)
	    if(stripe[i].ptr)
		c ^= *(UINTN *)(stripe[i].ptr + j);
	*(UINTN *)(dst + j) = c;
    }
}

/**
 * Unpin each cached stripe through its originating block device.
 * The stripe array and the physical device remain caller-owned.
 */
static void stripe_release(struct stripe_table *stripe, int count, uint32_t offset)
{
    unsigned i;
    for(i = 0; i < count; i++) {
	if(stripe[i].ptr) {
	    fsw_block_release(stripe[i].dev, stripe[i].off + offset, (void *)stripe[i].ptr);
	}
    }
}

static uint8_t powx[255 * 2];

static unsigned powx_inv[256];
static const uint8_t poly = 0x1d;
/**
 * Multiply nonzero GF(2^8) bytes in place through the RAID6 lookup
 * tables, leaving zero bytes zero. The tables must be initialised and the
 * multiplier valid; this leaf operation does not test either assumption.
 */
static void block_mulx (unsigned mul, char *buf, uint32_t size)
{
    uint32_t i;
    uint8_t *p = (uint8_t *) buf;
    for (i = 0; i < size; i++, p++)
	if (*p)
	    *p = powx[mul + powx_inv[*p]];
}
/**
 * XOR the finite-field-scaled source stripe into destination bytes.
 * The source is borrowed. Lookup-table and multiplier assumptions
 * are identical to the in-place block_mulx operation.
 */
static void block_mulx_xor (char *dst, unsigned mul, const char *buf, uint32_t size)
{
    uint32_t i;
    const uint8_t *p = (const uint8_t *) buf;
    uint8_t *q = (uint8_t *) dst;
    for (i = 0; i < size; i++, p++, q++)
	if (*p)
	    *q ^= powx[mul + powx_inv[*p]];
}

/*
 * Build byte-wise finite-field lookup tables used by RAID6 stripe
 * reconstruction. The power table is duplicated across 255 indices for
 * wraparound and indexed through a reverse table for nonzero operands. The
 * one-time initialized flag is process-global; this function establishes
 * arithmetic tables, not that all required stripes are available.
 */
static void raid6_init_table (void)
{
    static int initialized = 0;
    unsigned i;

    if(initialized)
	return;

    uint8_t cur = 1;
    for (i = 0; i < 255; i++)
    {
	powx[i] = cur;
	powx[i + 255] = cur;
	powx_inv[cur] = i;
	if (cur & 0x80)
	    cur = (cur << 1) ^ poly;
	else
	    cur <<= 1;
    }
    initialized = 1;
}

/**
 * A direct-mapped 17-slot cache holds reconstructed sectors. The hash chooses a
 * candidate slot only. Device ID and byte offset must still match or the cached
 * data is invalidated. Collisions reuse its allocation, which remains owned by
 * the volume.
 */
static struct fsw_btrfs_recover_cache *get_recover_cache(struct fsw_btrfs_volume *vol, uint64_t device_id, uint64_t offset)
{
    if(vol->rcache == NULL) {
	if(fsw_alloc_zero(sizeof(struct fsw_btrfs_recover_cache) * RECOVER_CACHE_SIZE, (void **)&vol->rcache) != FSW_SUCCESS)
	    return NULL;
    }
#ifdef __MAKEWITH_TIANO
    unsigned hash;
#else
    UINTN hash;
#endif
    DivU64x32Remainder(((device_id >> 32) | device_id | (offset >> 32) | offset), RECOVER_CACHE_SIZE, &hash);
    struct fsw_btrfs_recover_cache *rc = &vol->rcache[hash];
    if(rc->buffer == NULL) {
	if(fsw_alloc_zero(vol->sectorsize, (void **)&rc->buffer) != FSW_SUCCESS)
	    return NULL;
    }
    if(rc->device_id != device_id || rc->offset != offset) {
	rc->valid = FALSE;
	rc->device_id = device_id;
	rc->offset = offset;
    }
    return rc;
}

/*
 * Translates Btrfs logical addresses through filesystem mapping metadata.
 * Readability depends on correct mapping and available backing devices.
 */
static fsw_status_t fsw_btrfs_read_logical (struct fsw_btrfs_volume *vol, uint64_t addr,
        void *buf, fsw_size_t size, int rdepth, int cache_level)
{
    struct stripe_table *stripe_table = NULL;
    int challoc = 0;
    struct btrfs_chunk_item *chunk = NULL;
    fsw_status_t err = 0;
    while (size > 0)
    {
        uint8_t *ptr;
        struct btrfs_key *key;
        uint64_t csize;
        struct btrfs_key key_out;
        struct btrfs_key key_in;
        fsw_size_t chsize;
        uint64_t chaddr;

	err = 0;
        /*
         * Chunk-tree nodes have logical addresses too. First consult the
         * system chunk mappings copied from the superblock. They supply the
         * physical reads needed to traverse the chunk tree for all other
         * mappings.
         */
        for (ptr = vol->bootstrap_mapping; ptr < vol->bootstrap_mapping + sizeof (vol->bootstrap_mapping) - sizeof (struct btrfs_key);)
        {
            key = (struct btrfs_key *) ptr;
            if (key->type != GRUB_BTRFS_ITEM_TYPE_CHUNK)
                break;
            chunk = (struct btrfs_chunk_item *) (key + 1);
            if (fsw_u64_le_swap (key->offset) <= addr
                    && addr < fsw_u64_le_swap (key->offset)
                    + fsw_u64_le_swap (chunk->size))
            {
                goto chunk_found;
            }
            ptr += sizeof (*key) + sizeof (*chunk)
                + sizeof (struct btrfs_chunk_stripe)
                * fsw_u16_le_swap (chunk->nstripes);
        }

        key_in.object_id = fsw_u64_le_swap (GRUB_BTRFS_OBJECT_ID_CHUNK);
        key_in.type = GRUB_BTRFS_ITEM_TYPE_CHUNK;
        key_in.offset = fsw_u64_le_swap (addr);
        err = lower_bound (vol, &key_in, &key_out, vol->chunk_tree, &chaddr, &chsize, NULL, rdepth);
        if (err)
            return err;
        key = &key_out;
        if (key->type != GRUB_BTRFS_ITEM_TYPE_CHUNK
                || !(fsw_u64_le_swap (key->offset) <= addr))
        {
            return FSW_VOLUME_CORRUPTED;
        }

        chunk = AllocatePool (chsize);
        if (!chunk) {
            return FSW_OUT_OF_MEMORY;
        }

        challoc = 1;
        err = fsw_btrfs_read_logical (vol, chaddr, chunk, chsize, rdepth, cache_level < 5 ? cache_level+1 : 5);
        if (err)
	    goto io_error;

chunk_found:
        {
#ifdef __MAKEWITH_GNUEFI
#define UINTREM UINTN
#else
#undef DivU64x32
#define DivU64x32 DivU64x32Remainder
#define UINTREM UINT32
#endif
            UINTREM stripen;
            UINTREM stripeq;
            UINTREM stripe_offset;
            uint64_t off = addr - fsw_u64_le_swap (key->offset);
            unsigned redundancy = 1;
            unsigned i;

            if (fsw_u64_le_swap (chunk->size) <= off)

                goto volume_corrupted;

	    uint16_t nstripes = fsw_u16_le_swap (chunk->nstripes);
            DPRINT(L"btrfs chunk 0x%lx+0xlx %d stripes (%d substripes) of %lx\n",
                    fsw_u64_le_swap (key->offset),
                    fsw_u64_le_swap (chunk->size),
                    nstripes,
                    fsw_u16_le_swap (chunk->nsubstripes),
                    fsw_u64_le_swap (chunk->stripe_length));
            switch (fsw_u64_le_swap (chunk->type)
                    & ~GRUB_BTRFS_CHUNK_TYPE_BITS_DONTCARE)
            {
                case GRUB_BTRFS_CHUNK_TYPE_SINGLE:
                    {
                        uint64_t stripe_length;
                        stripe_length = DivU64x32 (fsw_u64_le_swap (chunk->size), nstripes, NULL);
                        if(stripe_length >= 1ULL<<32)
                            return FSW_VOLUME_CORRUPTED;
                        stripen = DivU64x32 (off, (uint32_t)stripe_length, &stripe_offset);
                        csize = (stripen + 1) * stripe_length - off;
                        DPRINT(L"read_logical %d chunk_found single csize=%d\n", __LINE__, csize);
                        break;
                    }
                case GRUB_BTRFS_CHUNK_TYPE_RAID1C4:
                    redundancy++;

                case GRUB_BTRFS_CHUNK_TYPE_RAID1C3:
                    redundancy++;

                case GRUB_BTRFS_CHUNK_TYPE_DUPLICATED:
                case GRUB_BTRFS_CHUNK_TYPE_RAID1:
                    {
                        stripen = 0;
                        stripe_offset = off;
                        csize = fsw_u64_le_swap (chunk->size) - off;
                        redundancy++;
                        DPRINT(L"read_logical %d chunk_found dup/raid1 off=%lx csize=%d redundancy=%d\n", __LINE__, stripe_offset, csize, redundancy);
                        break;
                    }
                case GRUB_BTRFS_CHUNK_TYPE_RAID0:
                    {
                        uint64_t stripe_length = fsw_u64_le_swap (chunk->stripe_length);
                        uint64_t middle, high;
                        UINTREM low;

                        if(stripe_length > 1UL<<30)
                            return FSW_VOLUME_CORRUPTED;

                        middle = DivU64x32 (off, (uint32_t)stripe_length, &low);

                        high = DivU64x32 (middle, nstripes, &stripen);
                        stripe_offset =
                            low + fsw_u64_le_swap (chunk->stripe_length) * high;
                        csize = fsw_u64_le_swap (chunk->stripe_length) - low;
                        DPRINT(L"read_logical %d chunk_found raid0 csize=%d\n", __LINE__, csize);
                        break;
                    }
                case GRUB_BTRFS_CHUNK_TYPE_RAID10:
                    {
                        uint64_t stripe_length = fsw_u64_le_swap (chunk->stripe_length);
                        uint64_t middle, high;
                        UINTREM low;

                        if(stripe_length > 1UL<<30)
                            return FSW_VOLUME_CORRUPTED;

                        middle = DivU64x32 (off, stripe_length, &low);

                        high = DivU64x32 (middle, nstripes / fsw_u16_le_swap (chunk->nsubstripes), &stripen);
                        stripen *= fsw_u16_le_swap (chunk->nsubstripes);
                        redundancy = fsw_u16_le_swap (chunk->nsubstripes);
                        stripe_offset = low + fsw_u64_le_swap (chunk->stripe_length)
                            * high;
                        csize = fsw_u64_le_swap (chunk->stripe_length) - low;
                        DPRINT(L"read_logical %d chunk_found raid01 csize=%d\n", __LINE__, csize);
                        break;
                    }
                case GRUB_BTRFS_CHUNK_TYPE_RAID5:
                case GRUB_BTRFS_CHUNK_TYPE_RAID6:
                    {
                        uint64_t stripe_length = fsw_u64_le_swap (chunk->stripe_length);
                        uint64_t middle, high;
			uint16_t nparities = (fsw_u64_le_swap(chunk->type) & GRUB_BTRFS_CHUNK_TYPE_RAID6) ? 2 : 1;
                        UINTREM low;

                        if(stripe_length > 1UL<<30 || nstripes > 255)
                            goto volume_corrupted;

                        middle = DivU64x32 (off, stripe_length, &low);

                        high = DivU64x32 (middle, nstripes - nparities, &stripen);
			DivU64x32(high + stripen, nstripes, &stripen);
			if(nparities == 1) {
			    stripeq = RAID5_TAG;
			} else {
			    middle = DivU64x32(high + nstripes -1, nstripes, &stripeq);
			}
                        redundancy = RAID5_TAG;
                        stripe_offset = low + fsw_u64_le_swap (chunk->stripe_length) * high;
                        csize = fsw_u64_le_swap (chunk->stripe_length) - low;
                        DPRINT(L"read_logical %d chunk_found raid01 csize=%d\n", __LINE__, csize);
                        break;
                    }
                default:
                    DPRINT (L"btrfs: unsupported RAID\n");
                    err = FSW_UNSUPPORTED;
		    goto io_error;
            }
            if (csize == 0)

                goto volume_corrupted;

            if (csize > (uint64_t) size)
                csize = size;

	    if(redundancy < RAID5_TAG) {
begin_direct_read:
		err = 0;
                for (i = 0; !err && i < redundancy; i++)
                {
                    struct btrfs_chunk_stripe *stripe;
                    uint64_t paddr;
                    struct fsw_volume *dev;

                    stripe = (struct btrfs_chunk_stripe *) (chunk + 1);

                    stripe += stripen + i;

                    paddr = fsw_u64_le_swap (stripe->offset) + stripe_offset;

                    DPRINT (L"btrfs: chunk 0x%lx+0x%lx (%d stripes (%d substripes) of %lx) stripe %lx maps to 0x%lx\n",
                            fsw_u64_le_swap (key->offset),
                            fsw_u64_le_swap (chunk->size),
                            nstripes,
                            fsw_u16_le_swap (chunk->nsubstripes),
                            fsw_u64_le_swap (chunk->stripe_length),
                            stripen, stripe->offset);
                    DPRINT (L"btrfs: reading paddr 0x%lx for laddr 0x%lx\n", paddr, addr);

                    dev = find_device (vol, stripe->device_id);
                    if (!dev)
                        continue;

                    uint32_t off = paddr & (vol->sectorsize - 1);
                    paddr >>= vol->sectorshift;
                    uint64_t n = 0;
                    while(n < csize) {
                        char *buffer;
                        err = fsw_block_get(dev, paddr, cache_level, (void **)&buffer);
                        if(err)
                            break;
                        int s = vol->sectorsize - off;
                        if(s > csize - n)
                            s = csize - n;
                        fsw_memcpy(buf+n, buffer+off, s);
                        fsw_block_release(dev, paddr, (void *)buffer);

                        n += s;
                        off = 0;
                        paddr++;
                    }
                    DPRINT (L"read logical: err %d csize %d got %d\n",
                                    err, csize, n);
                    if(n>=csize)
                        break;
                }
                if (i == redundancy) {
		    if(do_rescan_once(vol) > 0)
			goto begin_direct_read;
		    if(err == 0)
                        goto volume_corrupted;
		}
		if (err)
		    goto io_error;

	    } else {

		struct btrfs_chunk_stripe *stripe = (struct btrfs_chunk_stripe *) (chunk + 1);
		unsigned sectorsize = vol->sectorsize;

		{
		    uint64_t sectormask = fsw_u64_le_swap(sectorsize - 1);
		    for(i = 0; i < nstripes; i++)
			if(stripe[i].offset & sectormask)
			    goto volume_corrupted;
		}

		struct fsw_volume *dev = find_device (vol, stripe[stripen].device_id);
		if(dev == NULL && do_rescan_once(vol) > 0)
		    dev = find_device (vol, stripe[stripen].device_id);

		uint32_t posN = stripen;
		BOOLEAN is_raid5 = stripeq == RAID5_TAG;
		uint32_t dstripes = nstripes - (is_raid5 ? 1 : 2);

		uint64_t n = 0;
		uint32_t off = stripe_offset & (sectorsize - 1);
		stripe_offset >>= vol->sectorshift;
		while(n < csize) {
		    int used_bytes = sectorsize - off;
		    if(used_bytes > csize - n)
			used_bytes = csize - n;
		    char *buffer;
		    struct fsw_btrfs_recover_cache *rcache = NULL;
		    uint64_t paddrN = (fsw_u64_le_swap (stripe[stripen].offset) >> vol->sectorshift) + stripe_offset;

		    if(dev && !(err = fsw_block_get(dev, paddrN, cache_level, (void **)&buffer))) {

                        fsw_memcpy(buf+n, buffer+off, used_bytes);
                        fsw_block_release(dev, paddrN, (void *)buffer);

		    } else if((rcache = get_recover_cache(vol, stripe[stripen].device_id, paddrN)) == NULL) {
			err = FSW_OUT_OF_MEMORY;
			goto io_error;
		    } else if(rcache->valid) {

                        fsw_memcpy(buf+n, rcache->buffer+off, used_bytes);

                    } else {

			if(!stripe_table) {

			    err = fsw_alloc_zero(sizeof(struct stripe_table) * nstripes, (void **)&stripe_table);
			    if(err)
				goto io_error;
			    unsigned dev_count = 0;
			    uint32_t stripeI = is_raid5 ? 0 : (stripeq + 1) % nstripes;
			    for(i = 0; i < nstripes; i++) {
				if(stripeI == stripen) {
				    stripe_table[i].dev = NULL;
				    posN = i;
				} else {
				    stripe_table[i].off = fsw_u64_le_swap (stripe[stripeI].offset) >> vol->sectorshift;
				    stripe_table[i].dev = find_device (vol, stripe[stripeI].device_id);
				    if(stripe_table[i].dev == NULL && do_rescan_once(vol) > 0)
					stripe_table[i].dev = find_device (vol, stripe[stripeI].device_id);
				    if(stripe_table[i].dev)
					dev_count ++;
				}
				stripeI = stripeI == nstripes -1 ? 0 : stripeI + 1;
			    }
			    if(dev_count < dstripes)

				goto volume_corrupted;
			}

			uint32_t bad2 = RAID5_TAG;
			err = 0;
			for(i = 0; i < nstripes; i++) {
			    stripe_table[i].ptr = NULL;
			    if(i == posN) {

			    } else {
				err = stripe_table[i].dev == NULL ? FSW_IO_ERROR :
				    fsw_block_get(stripe_table[i].dev, stripe_table[i].off + stripe_offset, cache_level, (void **)&(stripe_table[i].ptr));
				if(err) {
				    if(is_raid5 || bad2 != RAID5_TAG)

					break;

				    bad2 = i;
				    err = 0;
				} else {
				    if(i == dstripes  && bad2 == RAID5_TAG)

					break;
				}
			    }
			}

			char *pbuf;  
			if(err) {

			    stripe_release(stripe_table, i, stripe_offset);
			} else if(bad2 == RAID5_TAG) {

			    stripe_xor(rcache->buffer, stripe_table, i, sectorsize);
			    stripe_release(stripe_table, i+1, stripe_offset);
			} else {
			    raid6_init_table();

			    fsw_memzero(rcache->buffer, sectorsize);
			    for( i = 0; i < nstripes - 2; i++) {
				if(stripe_table[i].ptr)
				    block_mulx_xor(rcache->buffer, i, stripe_table[i].ptr, sectorsize);
			    }
			    block_xor(rcache->buffer,  stripe_table[nstripes - 1].ptr, sectorsize);

			    if(bad2 == nstripes - 2) {

				block_mulx(255 - posN, rcache->buffer, sectorsize);
			    } else if((err = fsw_alloc(sectorsize, (void **)&pbuf))==FSW_SUCCESS) {

				unsigned int c = ((255 ^ posN) + (255 ^ powx_inv[(powx[bad2 + (posN ^ 255)] ^ 1)]))%255;
				block_mulx (c, rcache->buffer, sectorsize);
				stripe_xor(pbuf, stripe_table, dstripes, sectorsize);
				block_mulx_xor(rcache->buffer, (bad2+c)%255, pbuf, sectorsize);
				fsw_free(pbuf);
			    }
			    stripe_release(stripe_table, nstripes, stripe_offset);
			}

			if(err)
			    goto io_error;

			fsw_memcpy(buf+n, rcache->buffer+off, used_bytes);
			rcache->valid = TRUE;
		    }

		    err = 0;
		    n += used_bytes;
		    off = 0;
		    stripe_offset++;
                    DPRINT (L"read logical: err %d csize %d got %d\n", err, csize, n);
                }
	    }
        }
        size -= csize;
        buf = (uint8_t *) buf + csize;
        addr += csize;
        if (challoc && chunk)
            FreePool (chunk);
        challoc = 0;
	if(stripe_table)
	    fsw_free(stripe_table);
	stripe_table = NULL;
    }
    return FSW_SUCCESS;

volume_corrupted:
    err = FSW_VOLUME_CORRUPTED;
io_error:
    if(challoc && chunk)
	FreePool (chunk);
    if(stripe_table)
	FreePool(stripe_table);
    return err;
}

static fsw_status_t fsw_btrfs_get_default_root(struct fsw_btrfs_volume *vol, uint64_t root_dir_objectid);
/**
 * Group physical members by filesystem UUID. The first becomes the master and
 * resolves the default root. Further members register their device IDs with that
 * master and expose a placeholder root. A multi-device master's first
 * missing-device read can trigger a single rescan.
 */
static fsw_status_t fsw_btrfs_volume_mount(struct fsw_volume *volg) {
    struct btrfs_superblock sblock;
    struct fsw_btrfs_volume *vol = (struct fsw_btrfs_volume *)volg;
    struct fsw_btrfs_volume *master_out = NULL;
    struct fsw_string s;
    fsw_status_t err;
    int i;

    init_crc32c_table();

    err = btrfs_read_superblock (volg, &sblock);
    if (err)
        return err;

    btrfs_set_superblock_info(vol, &sblock);

    if(vol->sectorshift == 0)
        return FSW_UNSUPPORTED;

    if(vol->num_devices >= BTRFS_MAX_NUM_DEVICES)
        return FSW_UNSUPPORTED;

    vol->is_master = master_uuid_add(vol, &master_out);
    if (vol->is_master < 0) {
        vol->is_master = 0;
        return FSW_OUT_OF_MEMORY;
    }

    if(vol->is_master == 0) {
#define FAKE_LABEL "btrfs.multi.device"
        s.type = FSW_STRING_TYPE_UTF8;
        s.size = s.len = sizeof(FAKE_LABEL)-1;
        s.data = FAKE_LABEL;
        err = fsw_strdup_coerce(&volg->label, volg->host_string_type, &s);
        if (err)
            return err;
        btrfs_add_multi_device(master_out, volg, &sblock);

        return fsw_dnode_create_root_with_tree(volg, 0, 0, &volg->root);
    }

    fsw_set_blocksize(volg, vol->sectorsize, vol->sectorsize);
    vol->n_devices_allocated = vol->num_devices;
    vol->rescan_once = vol->num_devices > 1;
    err = fsw_alloc(sizeof(struct fsw_btrfs_device_desc) * vol->n_devices_allocated,
	(void **)&vol->devices_attached);
    if (err)
        return err;

    vol->n_devices_attached = 1;
    vol->devices_attached[0].dev = volg;
    vol->devices_attached[0].id = sblock.this_device.device_id;

    for (i = 0; i < 0x100; i++)
        if (sblock.label[i] == 0)
            break;

    s.type = FSW_STRING_TYPE_UTF8;
    s.size = s.len = i;
    s.data = sblock.label;
    err = fsw_strdup_coerce(&volg->label, volg->host_string_type, &s);
    if (err) {
        FreePool (vol->devices_attached);
        vol->devices_attached = NULL;
        return err;
    }

    err = fsw_btrfs_get_default_root(vol, sblock.root_dir_objectid);
    if (err) {
        DPRINT(L"root not found\n");
        FreePool (vol->devices_attached);
        vol->devices_attached = NULL;
        return err;
    }

    return FSW_SUCCESS;
}

/**
 * Unregister a master and unmount attached slave volumes. Each recovery
 * cache slot owns a distinct buffer and must be released once before the
 * cache-array allocation is freed.
 */
static void fsw_btrfs_volume_free(struct fsw_volume *volg)
{
    unsigned i;
    struct fsw_btrfs_volume *vol = (struct fsw_btrfs_volume *)volg;

    if (vol==NULL)
        return;

    if (vol->is_master)
        master_uuid_remove(vol);

    if(vol->devices_attached) {

	for (i = 1; i < vol->n_devices_attached; i++) {
	    if(vol->devices_attached[i].dev)
		fsw_unmount (vol->devices_attached[i].dev);
	}
	FreePool (vol->devices_attached);
    }
    if(vol->extent)
        FreePool (vol->extent);
    if(vol->rcache) {
	for(i = 0; i < RECOVER_CACHE_SIZE; i++)
	    if(vol->rcache[i].buffer)
		FreePool(vol->rcache[i].buffer);
        FreePool (vol->rcache);
    }
}

/**
 * Report logical free bytes as total minus used, clamping inconsistent
 * used>total metadata to zero. Redundancy profiles and degraded devices
 * can reduce practical writable capacity below this estimate.
 */
static fsw_status_t fsw_btrfs_volume_stat(struct fsw_volume *volg, struct fsw_volume_stat *sb)
{
    struct fsw_btrfs_volume *vol = (struct fsw_btrfs_volume *)volg;
    sb->total_bytes = vol->total_bytes;
    sb->free_bytes = (vol->bytes_used <= vol->total_bytes)
                   ? vol->total_bytes - vol->bytes_used : 0;
    return FSW_SUCCESS;
}

/*
 * Resolve an inode using its B-tree identity and object number, then read the
 * inode body from the returned logical address. A matched key type is necessary
 * but not sufficient to demonstrate that the tree item's recorded byte span
 * contains an entire inode: this routine does not compare elemsize against
 * sizeof(*inode) before requesting the following logical read.
 */
static fsw_status_t fsw_btrfs_read_inode (struct fsw_btrfs_volume *vol,
        struct btrfs_inode *inode, uint64_t num,
        uint64_t tree)
{
    struct btrfs_key key_in, key_out;
    uint64_t elemaddr;
    fsw_size_t elemsize;
    fsw_status_t err;

    key_in.object_id = num;
    key_in.type = GRUB_BTRFS_ITEM_TYPE_INODE_ITEM;
    key_in.offset = 0;

    err = lower_bound (vol, &key_in, &key_out, tree, &elemaddr, &elemsize, NULL, 0);
    if (err)
        return err;
    if (num != key_out.object_id
            || key_out.type != GRUB_BTRFS_ITEM_TYPE_INODE_ITEM)
        return FSW_NOT_FOUND;

    return fsw_btrfs_read_logical (vol, elemaddr, inode, sizeof (*inode), 0, 2);
}

/*
 * Lazy inode metadata belong to a Btrfs tree and inode-number pair.
 * The tree identity is essential where different subvolumes reuse
 * the same inode numbers.
 */
static fsw_status_t fsw_btrfs_dnode_fill(struct fsw_volume *volg, struct fsw_dnode *dnog)
{
    struct fsw_btrfs_volume *vol = (struct fsw_btrfs_volume *)volg;
    struct fsw_btrfs_dnode *dno = (struct fsw_btrfs_dnode *)dnog;
    fsw_status_t    err;
    uint32_t mode;

    if (!vol->is_master) {
        dno->g.size = 0;
        dno->g.type = FSW_DNODE_TYPE_DIR;
        return FSW_SUCCESS;
    }

    if (dno->raw)
        return FSW_SUCCESS;

    dno->raw = AllocatePool(sizeof(struct btrfs_inode));
    if(dno->raw == NULL)
        return FSW_OUT_OF_MEMORY;

    err = fsw_btrfs_read_inode(vol, dno->raw, dno->g.dnode_id, dno->g.tree_id);
    if (err) {
        FreePool(dno->raw);
        dno->raw = NULL;
        return err;
    }

    dno->g.size = fsw_u64_le_swap(dno->raw->size);

    mode = fsw_u32_le_swap(dno->raw->mode);
    if (S_ISREG(mode))
        dno->g.type = FSW_DNODE_TYPE_FILE;
    else if (S_ISDIR(mode))
        dno->g.type = FSW_DNODE_TYPE_DIR;
    else if (S_ISLNK(mode))
        dno->g.type = FSW_DNODE_TYPE_SYMLINK;
    else
        dno->g.type = FSW_DNODE_TYPE_SPECIAL;

    return FSW_SUCCESS;
}

static void fsw_btrfs_dnode_free(struct fsw_volume *volg, struct fsw_dnode *dnog)
{
    struct fsw_btrfs_dnode *dno = (struct fsw_btrfs_dnode *)dnog;
    if (dno->raw)
        FreePool(dno->raw);
}

/* Export inode byte accounting, timestamps and POSIX mode if inode data
 * are cached. A synthetic root without a raw inode reports zero dates and
 * used bytes. This is on-disk metadata, not a readback of physical extents.
 */
static fsw_status_t fsw_btrfs_dnode_stat(struct fsw_volume *volg, struct fsw_dnode *dnog, struct fsw_dnode_stat *sb)
{
    struct fsw_btrfs_dnode *dno = (struct fsw_btrfs_dnode *)dnog;

    if(dno->raw == NULL) {
        sb->used_bytes = 0;
        fsw_store_time_posix(sb, FSW_DNODE_STAT_CTIME, 0);
        fsw_store_time_posix(sb, FSW_DNODE_STAT_ATIME, 0);
        fsw_store_time_posix(sb, FSW_DNODE_STAT_MTIME, 0);
        return FSW_SUCCESS;
    }
    sb->used_bytes = fsw_u64_le_swap(dno->raw->nbytes);
    fsw_store_time_posix(sb, FSW_DNODE_STAT_ATIME,
            fsw_u64_le_swap(dno->raw->atime.sec));
    fsw_store_time_posix(sb, FSW_DNODE_STAT_CTIME,
            fsw_u64_le_swap(dno->raw->ctime.sec));
    fsw_store_time_posix(sb, FSW_DNODE_STAT_MTIME,
            fsw_u64_le_swap(dno->raw->mtime.sec));
    fsw_store_attr_posix(sb, fsw_u32_le_swap(dno->raw->mode));

    return FSW_SUCCESS;
}

/*
 * Decode the Btrfs LZO page-framed representation, skipping complete pages
 * until the requested decompressed offset and copying the remaining output.
 * Four-byte little-endian block lengths and page-alignment padding come from
 * compressed input. The current code dereferences size words and advances
 * across compressed pages without a complete remaining-input check at each
 * step: checking only the advertised total size is insufficient. Truncated
 * or adversarial page headers remain an unqualified input-safety boundary.
 */
static fsw_ssize_t grub_btrfs_lzo_decompress(char *ibuf, fsw_size_t isize, grub_off_t off,
        char *obuf, fsw_size_t osize)
{
    uint32_t total_size, cblock_size;
    fsw_size_t ret = 0;
    unsigned char buf[GRUB_BTRFS_LZO_BLOCK_SIZE];
    char *ibuf0 = ibuf;

#define fsw_get_unaligned32(x) (*(uint32_t *)(x))
    total_size = fsw_u32_le_swap (fsw_get_unaligned32(ibuf));
    ibuf += sizeof (total_size);

    if (isize < total_size)
        return -1;

    while (off >= GRUB_BTRFS_LZO_BLOCK_SIZE)
    {

        if (((ibuf - ibuf0) & 0xffc) == 0xffc)
            ibuf = ((ibuf - ibuf0 + 3) & ~3) + ibuf0;

        cblock_size = fsw_u32_le_swap (fsw_get_unaligned32 (ibuf));
        ibuf += sizeof (cblock_size);

        if (cblock_size > GRUB_BTRFS_LZO_BLOCK_MAX_CSIZE)
            return -1;

        off -= GRUB_BTRFS_LZO_BLOCK_SIZE;
        ibuf += cblock_size;
    }

    while (osize > 0)
    {
        lzo_uint usize = GRUB_BTRFS_LZO_BLOCK_SIZE;

        if (((ibuf - ibuf0) & 0xffc) == 0xffc)
            ibuf = ((ibuf - ibuf0 + 3) & ~3) + ibuf0;

        cblock_size = fsw_u32_le_swap (fsw_get_unaligned32 (ibuf));
        ibuf += sizeof (cblock_size);

        if (cblock_size > GRUB_BTRFS_LZO_BLOCK_MAX_CSIZE)
            return -1;

        if (off > 0 || osize < GRUB_BTRFS_LZO_BLOCK_SIZE)
        {
            fsw_size_t to_copy = GRUB_BTRFS_LZO_BLOCK_SIZE - off;

            if (to_copy > osize)
                to_copy = osize;

            if (lzo1x_decompress_safe ((lzo_bytep)ibuf, cblock_size, (lzo_bytep)buf, &usize, NULL) != 0)
                return -1;

            if (to_copy > usize)
                to_copy = usize;
            fsw_memcpy(obuf, buf + off, to_copy);

            osize -= to_copy;
            ret += to_copy;
            obuf += to_copy;
            ibuf += cblock_size;
            off = 0;
            continue;
        }

        if (lzo1x_decompress_safe ((lzo_bytep)ibuf, cblock_size, (lzo_bytep)obuf, &usize, NULL) != 0)
            return -1;

        osize -= usize;
        ret += usize;
        obuf += usize;
        ibuf += cblock_size;
    }

    return ret;
}

#include "fsw_btrfs_zstd.h"

typedef fsw_ssize_t (*decompressor_t)(char *ibuf, fsw_size_t isize, grub_off_t off, char *obuf, fsw_size_t osize);
static decompressor_t btrfs_decompressor_table[GRUB_BTRFS_COMPRESSION_MAX] = {
	grub_zlib_decompress,
	grub_btrfs_lzo_decompress,
	zstd_decompress,
};

/*
 * Compression identifiers are one-based array indexes into zlib, LZO and
 * Zstandard decoders. This dispatcher does not validate comp before indexing
 * comp-1; callers must enforce the supported range before reaching it.
 * A successful decode still requires the outer extent reader to check the
 * exact number of bytes written before publishing the output buffer.
 */
static fsw_ssize_t btrfs_decompress(uint8_t comp,
	char *ibuf, fsw_size_t isize,
	grub_off_t off,
        char *obuf, fsw_size_t osize)
{
	return btrfs_decompressor_table[comp-1](ibuf, isize, off, obuf, osize);
}

/**
 * Use the triple (tree, inode, file byte range) to reuse cached extent metadata.
 * Inline data resides in the tree item. A regular extent supplies a logical disk
 * address and an offset into its backing data. A regular record with address zero
 * produces a sparse run.
 *
 * A compressed regular extent requires two distinct allocations, one for its
 * stored bytes and one for the decoded result. The input is released after
 * decoding. A length mismatch releases the output locally. Only success publishes
 * the output pointer and its sector count to the stream handle.
 */
static fsw_status_t fsw_btrfs_get_extent(struct fsw_volume *volg, struct fsw_dnode *dnog,
        struct fsw_extent *extent)
{
    struct fsw_btrfs_volume *vol = (struct fsw_btrfs_volume *)volg;
    uint64_t ino = dnog->dnode_id;
    uint64_t tree = dnog->tree_id;
    uint64_t pos0 = extent->log_start << vol->sectorshift;
    extent->type = FSW_EXTENT_TYPE_INVALID;
    extent->buffer = NULL;
    extent->log_count = 1;
    uint64_t pos = pos0;
    fsw_size_t csize;
    fsw_status_t err;
    uint64_t extoff;
    char *buf = NULL;
    uint64_t count;

    if (!vol->is_master)
        return FSW_NOT_FOUND;
    /*
     * A cached extent is valid for a tree, inode and half-open byte range
     * together. Subvolumes can reuse inode numbers, so matching only the
     * inode and file offset could return another subvolume's data.
     */
    if (!vol->extent || vol->extstart > pos || vol->extino != ino
            || vol->exttree != tree || vol->extend <= pos)
    {
        struct btrfs_key key_in, key_out;
        uint64_t elemaddr;
        fsw_size_t elemsize;

        if(vol->extent) {
            FreePool (vol->extent);
            vol->extent = NULL;
        }
        key_in.object_id = ino;
        key_in.type = GRUB_BTRFS_ITEM_TYPE_EXTENT_ITEM;
        key_in.offset = fsw_u64_le_swap (pos);
        err = lower_bound (vol, &key_in, &key_out, tree, &elemaddr, &elemsize, NULL, 0);
        if (err)
            return FSW_VOLUME_CORRUPTED;
        if (key_out.object_id != ino
                || key_out.type != GRUB_BTRFS_ITEM_TYPE_EXTENT_ITEM)
        {
            return FSW_VOLUME_CORRUPTED;
        }
        if ((fsw_ssize_t) elemsize < ((char *) &vol->extent->inl
                    - (char *) vol->extent))
        {
            return FSW_VOLUME_CORRUPTED;
        }
        vol->extstart = fsw_u64_le_swap (key_out.offset);
        vol->extsize = elemsize;
        vol->extent = AllocatePool (elemsize);
        vol->extino = ino;
        vol->exttree = tree;
        if (!vol->extent)
            return FSW_OUT_OF_MEMORY;

        err = fsw_btrfs_read_logical (vol, elemaddr, vol->extent, elemsize, 0, 1);
        if (err)
            return err;

        vol->extend = vol->extstart + fsw_u64_le_swap (vol->extent->size);
        if (vol->extent->type == GRUB_BTRFS_EXTENT_REGULAR
                && (char *) vol->extent + elemsize
                >= (char *) &vol->extent->filled + sizeof (vol->extent->filled))
            vol->extend =
                vol->extstart + fsw_u64_le_swap (vol->extent->filled);

        DPRINT (L"btrfs: %lx +0x%lx\n", fsw_u64_le_swap (key_out.offset), fsw_u64_le_swap (vol->extent->size));
        if (vol->extend <= pos)
        {
            return FSW_VOLUME_CORRUPTED;
        }
    }

    csize = vol->extend - pos;
    extoff = pos - vol->extstart;

    if (vol->extent->encryption ||vol->extent->encoding)
    {
        return FSW_UNSUPPORTED;
    }

    switch(vol->extent->compression) {
        case GRUB_BTRFS_COMPRESSION_LZO:
        case GRUB_BTRFS_COMPRESSION_ZLIB:
        case GRUB_BTRFS_COMPRESSION_ZSTD:
        case GRUB_BTRFS_COMPRESSION_NONE:
            break;
        default:
            return FSW_UNSUPPORTED;
    }

    count = ( csize + vol->sectorsize - 1) >> vol->sectorshift;
    switch (vol->extent->type)
    {
        case GRUB_BTRFS_EXTENT_INLINE:
            buf = AllocatePool( count << vol->sectorshift);
            if(!buf)
                return FSW_OUT_OF_MEMORY;
            if (vol->extent->compression == GRUB_BTRFS_COMPRESSION_NONE)
                fsw_memcpy (buf, vol->extent->inl + extoff, csize);
            else if (btrfs_decompress (vol->extent->compression,
				vol->extent->inl, vol->extsize -
                            ((uint8_t *) vol->extent->inl
                             - (uint8_t *) vol->extent),
                            extoff, buf, csize)
                        != (fsw_ssize_t) csize)
	    {
                FreePool(buf);
                return FSW_VOLUME_CORRUPTED;
	    }
            break;

        case GRUB_BTRFS_EXTENT_REGULAR:
            if (!vol->extent->laddr)
                break;

            if (vol->extent->compression == GRUB_BTRFS_COMPRESSION_NONE)
            {
                if( count > 64 ) {
                    count = 64;
                    csize = count << vol->sectorshift;
                }
                buf = AllocatePool( count << vol->sectorshift);
                if(!buf)
                    return FSW_OUT_OF_MEMORY;
                err = fsw_btrfs_read_logical (vol,
                        fsw_u64_le_swap (vol->extent->laddr)
                        + fsw_u64_le_swap (vol->extent->offset)
                        + extoff, buf, csize, 0, 0);
                if (err) {
                    FreePool(buf);
                    return err;
                }
                break;
            }

            if (vol->extent->compression > GRUB_BTRFS_COMPRESSION_MAX)
                    return -FSW_VOLUME_CORRUPTED;

            {
                char *tmp;
                uint64_t zsize;
                fsw_ssize_t ret;

                zsize = fsw_u64_le_swap (vol->extent->compressed_size);
                tmp = AllocatePool (zsize);
                if (!tmp)
                    return -FSW_OUT_OF_MEMORY;
                err = fsw_btrfs_read_logical (vol, fsw_u64_le_swap (vol->extent->laddr), tmp, zsize, 0, 0);
                if (err)
                {
                    FreePool (tmp);
                    return -FSW_VOLUME_CORRUPTED;
                }

                buf = AllocatePool( count << vol->sectorshift);
                if(!buf) {
                    FreePool(tmp);
                    return FSW_OUT_OF_MEMORY;
                }

		ret = btrfs_decompress ( vol->extent->compression,
			tmp, zsize,
			extoff + fsw_u64_le_swap (vol->extent->offset),
			buf, csize);
                /* The decoder borrows both buffers. Only a complete output
                 * transfers to extent->buffer at the end of this function. */
                FreePool (tmp);

                if (ret != (fsw_ssize_t) csize) {
                    FreePool(buf);
                    return FSW_VOLUME_CORRUPTED;
                }

                break;
            }
            break;
        default:
            return -FSW_VOLUME_CORRUPTED;
    }

    extent->log_count = count;
    if(buf) {
        if(csize < (count << vol->sectorshift))
            fsw_memzero( buf + csize, (count << vol->sectorshift) - csize);
        extent->buffer = buf;
        extent->type = FSW_EXTENT_TYPE_BUFFER;
    } else {
        extent->buffer = NULL;
        extent->type = FSW_EXTENT_TYPE_SPARSE;
    }
    return FSW_SUCCESS;
}

/* Read a symlink from buffer-backed extents into one temporary UTF-8 buffer,
 * capped by FSW_PATH_MAX. Each source extent is freed after copying and the
 * temporary string is coerced into the host encoding for the caller.
 * The link read cannot succeed through an unmapped or non-buffer extent.
 */
static fsw_status_t fsw_btrfs_readlink(struct fsw_volume *volg, struct fsw_dnode *dnog,
        struct fsw_string *link_target)
{
    struct fsw_btrfs_volume *vol = (struct fsw_btrfs_volume *)volg;
    struct fsw_btrfs_dnode *dno = (struct fsw_btrfs_dnode *)dnog;
    int i;
    fsw_status_t    status;
    struct fsw_string s;
    char *tmp;

    if (dno->g.size > FSW_PATH_MAX)
        return FSW_VOLUME_CORRUPTED;

    tmp = AllocatePool(dno->g.size);
    if(!tmp)
        return FSW_OUT_OF_MEMORY;

    i = 0;
    do {
        struct fsw_extent extent;
        int size;
        extent.log_start = i;
        status = fsw_btrfs_get_extent(volg, dnog, &extent);
        if(status || extent.type != FSW_EXTENT_TYPE_BUFFER) {
            FreePool(tmp);
            if(extent.buffer)
                FreePool(extent.buffer);
            return FSW_VOLUME_CORRUPTED;
        }
        size = extent.log_count << vol->sectorshift;
        if(size > (dno->g.size - (i<<vol->sectorshift)))
            size = dno->g.size - (i<<vol->sectorshift);
        fsw_memcpy(tmp + (i<<vol->sectorshift), extent.buffer, size);
        FreePool(extent.buffer);
        i += extent.log_count;
    } while( (i << vol->sectorshift) < dno->g.size);

    s.type = FSW_STRING_TYPE_UTF8;
    s.size = s.len = (int)dno->g.size;
    s.data = tmp;
    status = fsw_strdup_coerce(link_target, volg->host_string_type, &s);
    FreePool(tmp);

    return FSW_SUCCESS;
}

/**
 * Use the CRC-32C name hash as the directory-item key. Multiple names can share
 * that key. Walk the packed candidates using their individual name and data
 * lengths, then require an exact length and byte match before returning a pointer
 * into the allocated item.
 */
static fsw_status_t fsw_btrfs_lookup_dir_item(struct fsw_btrfs_volume *vol,
        uint64_t tree_id, uint64_t object_id,
        struct fsw_string *lookup_name,
        struct btrfs_dir_item **direl_buf,
        struct btrfs_dir_item **direl_out
        )
{
    uint64_t elemaddr;
    fsw_size_t elemsize;
    fsw_size_t allocated = 0;
    struct btrfs_key key;
    struct btrfs_key key_out;
    struct btrfs_dir_item *cdirel;
    fsw_status_t err;

    *direl_buf = NULL;

    key.object_id = object_id;
    key.type = GRUB_BTRFS_ITEM_TYPE_DIR_ITEM;
    /*
     * The CRC-32C of the name selects a DIR_ITEM key. Several names can share
     * that key, so only the length and byte comparison inside the item
     * establishes a match.
     */
    key.offset = fsw_u64_le_swap (~grub_getcrc32c (1, lookup_name->data, lookup_name->size));

    err = lower_bound (vol, &key, &key_out, tree_id, &elemaddr, &elemsize, NULL, 0);
    if (err)
        return err;

    if (key_cmp (&key, &key_out) != 0)
        return FSW_NOT_FOUND;

    if (elemsize > allocated)
    {
        allocated = 2 * elemsize;
        if(*direl_buf)
            FreePool (*direl_buf);
        *direl_buf = AllocatePool (allocated + 1);
        if (!*direl_buf)
            return FSW_OUT_OF_MEMORY;
    }

    err = fsw_btrfs_read_logical (vol, elemaddr, *direl_buf, elemsize, 0, 1);
    if (err)
        return err;

    for (cdirel = *direl_buf;
            (uint8_t *) cdirel - (uint8_t *) *direl_buf < (fsw_ssize_t) elemsize;
            cdirel = (void *) ((uint8_t *) (*direl_buf + 1)
                + fsw_u16_le_swap (cdirel->n)
                + fsw_u16_le_swap (cdirel->m)))
    {
        if (lookup_name->size == fsw_u16_le_swap (cdirel->n)
                && fsw_memeq (cdirel->name, lookup_name->data, lookup_name->size))
            break;
    }
    if ((uint8_t *) cdirel - (uint8_t *) *direl_buf >= (fsw_ssize_t) elemsize)
        return FSW_NOT_FOUND;

    *direl_out = cdirel;
    return FSW_SUCCESS;
}

/* Resolve the root-tree item for a directory entry that crosses into a
 * subvolume. Search returns a candidate at-or-before the requested key, so
 * both object ID and item type are compared before returning the tree ID.
 */
static fsw_status_t fsw_btrfs_get_root_tree(
        struct fsw_btrfs_volume *vol,
        struct btrfs_key *key_in,
        uint64_t *tree_out)
{
    fsw_status_t err;
    struct btrfs_root_item ri;
    struct btrfs_key key_out;
    uint64_t elemaddr;
    fsw_size_t elemsize;

    err = lower_bound (vol, key_in, &key_out, vol->root_tree, &elemaddr, &elemsize, NULL, 0);
    if (err)
        return err;

    if (key_in->object_id != key_out.object_id || key_in->type != key_out.type)
        return FSW_NOT_FOUND;

    err = fsw_btrfs_read_logical (vol, elemaddr, &ri, sizeof (ri), 0, 1);
    if (err)
        return err;

    *tree_out = ri.tree;
    return FSW_SUCCESS;
}

/* Construct the child identity from a directory item. A subvolume-root
 * entry changes the tree identifier and resolves the root object; ordinary
 * entries retain the parent's tree and use their inode object ID. Preserve
 * this (tree ID, inode ID) pair when consulting the generic dnode cache.
 */
static fsw_status_t fsw_btrfs_get_sub_dnode(
        struct fsw_btrfs_volume *vol,
        struct fsw_btrfs_dnode *dno,
        struct btrfs_dir_item *cdirel,
        struct fsw_string *name,
        struct fsw_dnode **child_dno_out)
{
    fsw_status_t err;
    int child_type;
    uint64_t tree_id = dno->g.tree_id;
    uint64_t child_id;

    switch (cdirel->key.type)
    {
        case GRUB_BTRFS_ITEM_TYPE_ROOT_ITEM:
            err = fsw_btrfs_get_root_tree (vol, &cdirel->key, &tree_id);
            if (err)
                return err;

            child_type = GRUB_BTRFS_DIR_ITEM_TYPE_DIRECTORY;
            child_id = fsw_u64_le_swap(GRUB_BTRFS_OBJECT_ID_CHUNK);
            break;
        case GRUB_BTRFS_ITEM_TYPE_INODE_ITEM:
            child_type = cdirel->type;
            child_id = cdirel->key.object_id;
            break;

        default:
            DPRINT (L"btrfs: unrecognised object type 0x%x", cdirel->key.type);
            return FSW_VOLUME_CORRUPTED;
    }

    switch(child_type) {
        case GRUB_BTRFS_DIR_ITEM_TYPE_REGULAR:
            child_type = FSW_DNODE_TYPE_FILE;
            break;
        case GRUB_BTRFS_DIR_ITEM_TYPE_DIRECTORY:
            child_type = FSW_DNODE_TYPE_DIR;
            break;
        case GRUB_BTRFS_DIR_ITEM_TYPE_SYMLINK:
            child_type = FSW_DNODE_TYPE_SYMLINK;
            break;
        default:
            child_type = FSW_DNODE_TYPE_SPECIAL;
            break;
    }
    return fsw_dnode_create_with_tree(&dno->g, tree_id, child_id, child_type, name, child_dno_out);
}

/*
 * Finds a directory item within a selected tree; tree IDs and object IDs
 * are necessary parts of pathname identity.
 */
static fsw_status_t fsw_btrfs_dir_lookup(struct fsw_volume *volg, struct fsw_dnode *dnog,
        struct fsw_string *lookup_name, struct fsw_dnode **child_dno_out)
{
    struct fsw_btrfs_volume *vol = (struct fsw_btrfs_volume *)volg;
    struct fsw_btrfs_dnode *dno = (struct fsw_btrfs_dnode *)dnog;
    fsw_status_t err;
    struct fsw_string s;

    *child_dno_out = NULL;

    if (!vol->is_master)
        return FSW_NOT_FOUND;

    err = fsw_strdup_coerce(&s, FSW_STRING_TYPE_UTF8, lookup_name);
    if(err)
        return err;

    if(dnog == volg->root && s.size == 3 && ((char *)s.data)[0]=='.' && ((char *)s.data)[1]=='.' && ((char *)s.data)[2]=='.')
    {
        fsw_strfree (&s);
        if(dnog->tree_id == vol->top_tree) {
            fsw_dnode_retain(dnog);
            *child_dno_out = dnog;
            return FSW_SUCCESS;
        }
        return fsw_dnode_create_with_tree(dnog,
                vol->top_tree, fsw_u64_le_swap(GRUB_BTRFS_OBJECT_ID_CHUNK),
                FSW_DNODE_TYPE_DIR, lookup_name, child_dno_out);
    }
    struct btrfs_dir_item *direl=NULL, *cdirel;
    err = fsw_btrfs_lookup_dir_item(vol, dnog->tree_id, dnog->dnode_id, &s, &direl, &cdirel);
    if(!err)
        err = fsw_btrfs_get_sub_dnode(vol, dno, cdirel, lookup_name, child_dno_out);
    if(direl)
        FreePool (direl);
    fsw_strfree (&s);
    return err;
}

/*
 * Selects the default filesystem root/subvolume from tree metadata. Boot
 * scanning must not assume that a single top-level root exposes every OS.
 */
static fsw_status_t fsw_btrfs_get_default_root(struct fsw_btrfs_volume *vol, uint64_t root_dir_objectid)
{
    fsw_status_t err;
    struct fsw_string s;
    struct btrfs_dir_item *direl=NULL, *cdirel;
    struct btrfs_key top_root_key;

    top_root_key.object_id = fsw_u64_le_swap(5UL);
    top_root_key.type = GRUB_BTRFS_ITEM_TYPE_ROOT_ITEM;
    top_root_key.offset = -1LL;
    err = fsw_btrfs_get_root_tree (vol, &top_root_key, &vol->top_tree);
    if (err)
        return err;

    uint64_t default_tree_id = vol->top_tree;

    s.type = FSW_STRING_TYPE_UTF8;
    s.data = "default";
    s.size = 7;
    err = fsw_btrfs_lookup_dir_item(vol, vol->root_tree, root_dir_objectid, &s, &direl, &cdirel);

    if (!err &&  
            cdirel->type == GRUB_BTRFS_DIR_ITEM_TYPE_DIRECTORY &&  
            cdirel->key.type == GRUB_BTRFS_ITEM_TYPE_ROOT_ITEM &&  
            cdirel->key.object_id != fsw_u64_le_swap(5UL))
	fsw_btrfs_get_root_tree (vol, &cdirel->key, &default_tree_id);

    err = fsw_dnode_create_root_with_tree(&vol->g, default_tree_id,
	    fsw_u64_le_swap (GRUB_BTRFS_OBJECT_ID_CHUNK), &vol->g.root);
    if (direl)
        FreePool (direl);
    return err;
}

/*
 * Enumerate directory items via the stream's backend-specific cursor.
 * Subvolume and root references require their tree context; exhausting
 * a directory is not equivalent to exhausting the root tree.
 */
static fsw_status_t fsw_btrfs_dir_read(struct fsw_volume *volg, struct fsw_dnode *dnog,
        struct fsw_shandle *shand, struct fsw_dnode **child_dno_out)
{
    struct fsw_btrfs_volume *vol = (struct fsw_btrfs_volume *)volg;
    struct fsw_btrfs_dnode *dno = (struct fsw_btrfs_dnode *)dnog;
    fsw_status_t err;

    struct btrfs_key key_in, key_out;
    uint64_t elemaddr;
    fsw_size_t elemsize;
    fsw_size_t allocated = 0;
    struct btrfs_dir_item *direl = NULL;
    struct fsw_btrfs_leaf_descriptor desc;
    int r = 0;
    uint64_t tree = dnog->tree_id;

    if (!vol->is_master)
        return FSW_NOT_FOUND;

    key_in.object_id = dnog->dnode_id;
    key_in.type = GRUB_BTRFS_ITEM_TYPE_DIR_ITEM;
    key_in.offset = shand->pos;

    if((int64_t)key_in.offset == -1LL)
    {
        return FSW_NOT_FOUND;
    }

    err = lower_bound (vol, &key_in, &key_out, tree, &elemaddr, &elemsize, &desc, 0);
    if (err) {
        return err;
    }

    DPRINT(L"key_in %lx:%x:%lx out %lx:%x:%lx elem %lx+%lx\n",
            key_in.object_id, key_in.type, key_in.offset,
            key_out.object_id, key_out.type, key_out.offset,
            elemaddr, elemsize);
    if (key_out.type != GRUB_BTRFS_ITEM_TYPE_DIR_ITEM ||
            key_out.object_id != key_in.object_id)
    {
        r = next (vol, &desc, &elemaddr, &elemsize, &key_out);
        if (r <= 0)
            goto out;
        DPRINT(L"next out %lx:%x:%lx\n",
                key_out.object_id, key_out.type, key_out.offset, elemaddr, elemsize);
    }
    if (key_out.type == GRUB_BTRFS_ITEM_TYPE_DIR_ITEM &&
            key_out.object_id == key_in.object_id &&
            fsw_u64_le_swap(key_out.offset) <= fsw_u64_le_swap(key_in.offset))
    {
        r = next (vol, &desc, &elemaddr, &elemsize, &key_out);
        if (r <= 0)
            goto out;
        DPRINT(L"next out %lx:%x:%lx\n",
                key_out.object_id, key_out.type, key_out.offset, elemaddr, elemsize);
    }

    do
    {
        struct btrfs_dir_item *cdirel;
        if (key_out.type != GRUB_BTRFS_ITEM_TYPE_DIR_ITEM ||
                key_out.object_id != key_in.object_id)
        {
            r = 0;
            break;
        }
        if (elemsize > allocated)
        {
            allocated = 2 * elemsize;
            if(direl)
                FreePool (direl);
            direl = AllocatePool (allocated + 1);
            if (!direl)
            {
                r = -FSW_OUT_OF_MEMORY;
                break;
            }
        }

        err = fsw_btrfs_read_logical (vol, elemaddr, direl, elemsize, 0, 1);
        if (err)
        {
            r = -err;
            break;
        }

        for (cdirel = direl;
                (uint8_t *) cdirel - (uint8_t *) direl
                < (fsw_ssize_t) elemsize;
                cdirel = (void *) ((uint8_t *) (direl + 1)
                    + fsw_u16_le_swap (cdirel->n)
                    + fsw_u16_le_swap (cdirel->m)))
        {
            struct fsw_string s;
            s.type = FSW_STRING_TYPE_UTF8;
            s.size = s.len = fsw_u16_le_swap (cdirel->n);
            s.data = cdirel->name;
            DPRINT(L"item key %lx:%x%lx, type %lx, namelen=%lx\n",
                    cdirel->key.object_id, cdirel->key.type, cdirel->key.offset, cdirel->type, s.size);
            if(!err) {
                err = fsw_btrfs_get_sub_dnode(vol, dno, cdirel, &s, child_dno_out);
                if(direl)
                    FreePool (direl);
                free_iterator (&desc);
                shand->pos = key_out.offset;
                return FSW_SUCCESS;
            }
        }
        r = next (vol, &desc, &elemaddr, &elemsize, &key_out);
        DPRINT(L"next2 out %lx:%x:%lx\n",
                key_out.object_id, key_out.type, key_out.offset, elemaddr, elemsize);
    }
    while (r > 0);

out:
    if(direl)
        FreePool (direl);
    free_iterator (&desc);

    r = r < 0 ? -r : FSW_NOT_FOUND;
    return r;
}

struct fsw_fstype_table   FSW_FSTYPE_TABLE_NAME(btrfs) = {
    { FSW_STRING_TYPE_UTF8, 5, 5, "btrfs" },
    sizeof(struct fsw_btrfs_volume),
    sizeof(struct fsw_btrfs_dnode),

    fsw_btrfs_volume_mount,
    fsw_btrfs_volume_free,
    fsw_btrfs_volume_stat,
    fsw_btrfs_dnode_fill,
    fsw_btrfs_dnode_free,
    fsw_btrfs_dnode_stat,
    fsw_btrfs_get_extent,
    fsw_btrfs_dir_lookup,
    fsw_btrfs_dir_read,
    fsw_btrfs_readlink,
};
