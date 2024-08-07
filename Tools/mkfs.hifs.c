/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#include "tools.h"

#include "../hifs_shared_defs.h"

#define root_uid 0
#define root_gid 0
#define root_perms 644


struct command_line_flags flags;
const char * program_name = "mkfs.hifs";
int itable_zeroed = 0;
int fd;

off_t ALLOCATIONGRANULARITY=65536; // max(linux.mmap.ALLOCATIONGRANULARITY, windows.mmap.ALLOCATIONGRANULARITY)


static struct option long_options[] =
{
    {"help",         no_argument,       0, 'h'},
    {"force",        no_argument,       0, 'f'},
	{"verbose",      optional_argument, 0, 'v'},
	{"lazy_init",    no_argument,       0, 'l'},
    {"label",        no_argument,       0, 'L'},
    {"filename",     required_argument, 0, 'F'},
	{"mount",        required_argument, 0, 'm'},
    {"size",         required_argument, 0, 's'},
    {"noaction",     required_argument, 0, 'n'},
    {0, 0, 0, 0}
};


void usage()
{
    printf("Usage: mkfs.hifs [options] target_dir\n"
            "\n    initialize hifs superblock and cache disk\n\nOptions:\n"
            "  -h, --help            show this help message and exit\n"
            "  -f, --force           don't ask questions, just do it\n"
			"  -n,         --noaction=Do a run-through only, no actual action is taken\n"
			"  -v [LEVEL]  --verbose[=LEVEL] Verbose mode on, with optional level.\n"
			"                        level values=1 to 5 (highest) to set level of detail\n"
			"  -l,         --lazy_init Don't zero out the sectors before writing.\n"
			"						 Some potential for problems but init is faster.\n"
			"						 Sectors are zeroed when cache data is written only.\n"
            "  -L VOL_LABEL, --label=VOL_LABEL\n"
            "                        specify the volume label for device\n"
            "  -b BLOCKSIZE, --block-size=BLOCKSIZE (values in bytes)\n"
            "                        specify the block size to init for device\n"
            "  -F FILE/DEV-NAME, --filename=/dev/name or /file/name specify the device\n"
            "                        name or file name filesystem is to be built on\n"
            "  -m /dir/, --mount=/dir/  the directory to mount the filesystem on\n"
            "                        from the / root dir\n"
            "  -s SIZE,    --size=SIZE size in bytes of the filesystem to create (only\n"
            "                        if less than the size of the device/file created on)\n"	
            "  -o OVERFLOW, --overflow=OVERFLOW\n"
            "                        the overflow factor (default is 1.3)\n"
            "\nSamples:\n"
            "  mkfs.hifs -s 21474836480 -m /data/myfs\n"
            "  mkfs.hifs -s 21474836480 -b 65536 -m /data/myfs\n"
            "  mkfs.hifs -F /dev/sdb3 -m /data/myfs\n"
    );
}

static void create_journal_dev(ext2_filsys fs)
{
	struct ext2fs_progress *progress;
	errcode_t		retval;
	char			*buf;
	blk64_t			blk, err_blk;
	int			c, count, err_count;
	struct ext2fs_journal	*jparams;
	blk_t journal_block;

	// Specify the starting block of the journal (example value)
	journal_block = 8192; // This should be appropriately calculated or defined

	// Add a journal device
	retval = ext2fs_add_journal_device(fs, journal_block);
	if (retval) {
		com_err("hifs_mkfs", retval, "while adding journal device");
		ext2fs_close(fs);
		return -1;
	}

}

static void write_lost_and_found(ext2_filsys fs)
{
	unsigned int		lpf_size = 0;
	errcode_t		retval;
	ext2_ino_t		ino;
	const char		*name = "lost+found";
	int			i;

	fs->umask = 077;
	retval = ext2fs_mkdir(fs, EXT2_ROOT_INO, 0, name);
	if (retval) {
		com_err("ext2fs_mkdir", retval, "%d",
			_("while creating /lost+found"));
		exit(1);
	}

	retval = ext2fs_lookup(fs, EXT2_ROOT_INO, name, strlen(name), 0, &ino);
	if (retval) {
		com_err("ext2_lookup", retval, "%d",
			_("while looking up /lost+found"));
		exit(1);
	}

	for (i=1; i < EXT2_NDIR_BLOCKS; i++) {
		/* Ensure that lost+found is at least 2 blocks, so we always
		 * test large empty blocks for big-block filesystems.  */
		if ((lpf_size += fs->blocksize) >= 16*1024 &&
		    lpf_size >= 2 * fs->blocksize)
			break;
		retval = ext2fs_expand_dir(fs, ino);
		if (retval) {
			com_err("ext2fs_expand_dir", retval, "%d",
				_("while expanding /lost+found"));
			exit(1);
		}
	}
}

static void write_root_dir(ext2_filsys fs, int block, struct ext2_dir_entry *r_dentry)
{
	errcode_t		retval;
	struct ext2_inode	inode;
	int need_inode_change;

	retval = ext2fs_mkdir(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, 0);
	if (retval) {
		com_err("ext2fs_mkdir", retval, "%d",
			_("while creating root dir"));
		exit(1);
	}

	need_inode_change = (int)(root_uid != 0 || root_gid != 0 || root_perms != (mode_t)-1);

	if (need_inode_change) {
		retval = ext2fs_read_inode(fs, EXT2_ROOT_INO, &inode);
		if (retval) {
			com_err("ext2fs_read_inode", retval, "%d",
				_("while reading root inode"));
			exit(1);
		}
	}

	if (root_uid != 0 || root_gid != 0) {
		inode.i_uid = root_uid;
		ext2fs_set_i_uid_high(inode, root_uid >> 16);
		inode.i_gid = root_gid;
		ext2fs_set_i_gid_high(inode, root_gid >> 16);
	}

	if (root_perms != (mode_t)-1) {
		inode.i_mode = LINUX_S_IFDIR | root_perms;
	}

	if (need_inode_change) {
		retval = ext2fs_write_new_inode(fs, EXT2_ROOT_INO, &inode);
		if (retval) {
			com_err("ext2fs_write_inode", retval, "%d",
				_("while setting root inode ownership"));
			exit(1);
		}
	}
}

void write_new_inode(ext2_filsys fs, ext2_ino_t *ino, struct ext2_inode *inode) {
    errcode_t retval;

    // Allocate a new inode
    retval = ext2fs_new_inode(fs, 0, LINUX_S_IFREG | 0644, 0, ino);
    if (retval) {
        fprintf(stderr, "Error allocating new inode: %s\n", error_message(retval));
        exit(1);
    }

    // Write the inode data to the newly allocated inode
    retval = ext2fs_write_inode(fs, *ino, inode);
    if (retval) {
        fprintf(stderr, "Error writing inode: %s\n", error_message(retval));
        exit(1);
    }
}

void allocate_Special_Inode(ext2_filsys fs, ext2_ino_t ino, struct ext2_inode *inode) {
	time_t now;

	now = time(NULL);

    // Initialize the inode structure
    memset(&inode, 0, sizeof(struct ext2_inode));


	// Create a dentry for the file.
	struct ext2_dir_entry root_dentry;


	switch (ino)
	{
	case EXT2_BAD_INO:
		
		break;
	
	case EXT2_LAF_INO:
		/* code */
		break;
	
	case EXT2_USR_QUOTA_INO:
		/* code */
		break;
	
	case EXT2_GRP_QUOTA_INO:
		/* code */
		break;
	
	case EXT2_BOOT_LOADER_INO:
		/* code */
		break;
	
	case EXT2_UNDEL_DIR_INO:
		/* code */
		break;
	
	case EXT2_JOURNAL_INO:
		/* code */
		break;
	
	case EXT2_EXCLUDE_INO:
		/* code */
		break;
	
	case HIFS_ROOT_INODE:
		memset(&root_dentry, 0, sizeof(struct ext2_dir_entry));

		root_dentry.inode = HIFS_ROOT_INODE;
		root_dentry.rec_len = 7 + sizeof(flags.mount_point);
		root_dentry.name_len = sizeof(flags.mount_point);
		root_dentry.name[0] = ".";
		//root_dentry.file_type = EXT2_FT_DIR;
		int block = (HIFS_ROOT_DENTRY_OFFSET/flags.block_size);
		write_root_dir(fs, block, &root_dentry);
		break;
	
	default:
		return;
		break;
	}

	inode->i_uid = 0;
    inode->i_gid = 0;
    inode->i_mode = LINUX_S_IFREG | 0644;
    inode->i_size = HIFS_DEFAULT_BLOCK_SIZE;
	inode->i_atime = now;
	inode->i_ctime = now;
	inode->i_mtime = now;
	inode->i_dtime = now;
	inode->i_links_count = 1;
	inode->i_blocks = 1;
	inode->i_size_high = 0;

    // Allocate and write a new inode
    write_new_inode(fs, &ino, &inode);
    printf("Inode number [%u], pointing to block [%ls], allocated and written.\n", ino, inode->i_block);
	free(inode);
}

static void zap_sector(ext2_filsys fs, int sect, int nsect)
{
	char *buf;
	int retval;

zap_them:
	if (sect == 0) {
		buf = calloc(512, 1);
		if (!buf) {
			printf(_("Out of memory erasing sectors %d\n"), sect);
			exit(1);
		}	
		memset(buf, 0, 512);
		io_channel_set_blksize(fs->io, 512);
		retval = io_channel_write_blk64(fs->io, sect, -512*nsect, buf);
		if (nsect == 1) {
			goto its_done;
		}
		sect++; nsect--;
		goto zap_them;
	} else {
		buf = calloc(flags.block_size, nsect);
		if (!buf) {
			printf(_("Out of memory erasing sectors %d-%d\n"), sect, sect + nsect - 1);
			exit(1);
		}
		memset(buf, 0, flags.block_size*nsect);
		io_channel_set_blksize(fs->io, flags.block_size);
		retval = io_channel_write_blk64(fs->io, sect, -flags.block_size*nsect, buf);
		if ((sect * flags.block_size) >= HIFS_INODE_BITMAP_OFFSET && ((sect + nsect) + flags.block_size) >= HIFS_INODE_TABLE2_OFFSET) {
			itable_zeroed = 1;
		}
	}

its_done:
	io_channel_set_blksize(fs->io, fs->blocksize);
	free(buf);
	if (retval)
		fprintf(stderr, ("Warning: could not erase sector %d: %s\n"), sect, error_message(retval));
}

static void write_cache_table(ext2_filsys fs, struct hifs_cache_bitmap *cache_table, struct hifs_cache_bitmap *dirty_table)
{
	errcode_t	retval;
	dgrp_t		i;
	int		len = 0;
	struct ext2fs_progress *progress;
    struct ext2_inode *inode;
	int prog = 0;

	char *block = malloc(flags.block_size);
    if (!block) {
        perror("malloc");
        return;
    }
	memset(block, 0, flags.block_size);

	ext2fs_progress_init(fs, &progress, ("Writing inode tables: "), fs->group_desc_count);

	// Next let's zap the other sectors if lazy_init is not set.
	if (!itable_zeroed) {
		if (!flags.lazy_init) {
			zap_sector(fs, 1, 4); // Zap the inode tables if they haven't already been zeroed.
		}
	}

	// This would run out of memory quickly allocating 4096 bytes for each sector, all zeros, so it could write them out.
	// Instead, let's zap it 5 sectors at a time to avoid memory exhaustion. (It frees after every zap.)
	if (!flags.lazy_init) {
		for (i = 5; i < flags.blocks; i = i + 5) {
			if ((i + 5) < flags.blocks) {
				len = 5;
			} else {
				len = flags.blocks - i;
			}
			zap_sector(fs, i, len);
			ext2fs_progress_update(fs, &progress, prog + i);
		}
	}

	// Then write the inode bitmap, inode tables and dirty bitmap.
	// Start with a zeroed bitmap, then write it.
	io_channel_set_blksize(fs->io, flags.block_size);
	retval = io_channel_write_blk64(fs->io, 1, 1, cache_table->bitmap);	
	retval = io_channel_write_blk64(fs->io, 4, 1, dirty_table->bitmap);
	if (retval) {
		com_err("write_cache_table", retval, "%d", ("while writing cache bitmaps"));
		exit(1);
	}

	// Next zero out both tables and write those. (IE: tables with little actual data yet.)
		// There are a few questions:
				// ext2_fs inodes are 128 Bytes per inode.
				// With one Inode Tabloe block, we can have 32,768 inodes in cache.
				// With two blocks given to Inode Table, we can have 65,536.    <---<Default to EXT4, 2 Blocks>
				// ext4_fs inodes are 256 Bytes per inode.
				// One block = 16,384 inodes in cache.
				// Two blocks = 32,768 inodes in cache. 
	
	// We have a set of pre-determined inodes that we must populate, then write the cache table.
	for (i = 0; i < EXT2_EXCLUDE_INO; i++) {
		allocate_Special_Inode(fs, i, inode);
		ext2fs_progress_update(fs, &progress, prog + (i * 2));
	}

	// Allocate ROOT Dentry here.
	allocate_Special_Inode(fs, EXT2_ROOT_INO, inode);
	ext2fs_progress_update(fs, &progress, prog + 20);

	// Initialize the root directory block
    struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)block;

    // Create "." entry
    de->inode = HIFS_ROOT_INODE; // Root inode
    de->rec_len = 12; // Length of this directory entry
    de->name_len = 1; // Length of the name
    de->file_type = EXT2_FT_DIR; // Directory type
    strncpy(de->name, ".", de->name_len);

    // Create ".." entry
    de = (struct ext2_dir_entry_2 *)((char *)de + de->rec_len);
    de->inode = HIFS_ROOT_INODE; // Parent inode (root itself in this case)
    de->rec_len = flags.block_size - 12; // Length of this directory entry
    de->name_len = 2; // Length of the name
    de->file_type = EXT2_FT_DIR; // Directory type
    strncpy(de->name, "..", de->name_len);
	ext2fs_progress_update(fs, &progress, prog + 20);

    // Write the root directory block to the device
    if (pwrite(fd, block, flags.block_size, HIFS_ROOT_DENTRY_OFFSET * flags.block_size) != flags.block_size) {
        perror("root dentry write");
        close(fd);
        return -1;
    }

	ext2fs_progress_close(fs, &progress, ("done                            \n"));
}

int hifs_write_sblock(struct ext2_super_block *sb, struct hifs_cache_bitmap *cache_table, struct hifs_cache_bitmap *dirty_table)
{
	ext2_filsys fs_hifs;
	errcode_t err;

	// fopen();
	err = ext2fs_open(flags.device_name, EXT2_FLAG_RW, 0, 0, unix_io_manager, &fs_hifs);
    if (err) {
        fprintf(stderr, "Error opening filesystem: %s\n", error_message(err));
        return 1;
    }
	/*
	 * Wipe out the old on-disk superblock
	 */
	if (!flags.noaction)
		zap_sector(fs_hifs, 1, 4);

#ifdef ZAP_BOOTBLOCK
	zap_sector(fs_hifs, 0, 1);
#endif

    // lSeek() to the superblock position
    if (lseek(fd, HIFS_SUPER_OFFSET, SEEK_SET) < 0) {
        perror("lseek");
        return;
    }

	// write super;   
    // Write the superblock to the device
    if (write(fd, sb, sizeof(struct ext2_super_block)) != sizeof(struct ext2_super_block)) {
        perror("write");
        return -1;
    }

	write_cache_table(fs_hifs, cache_table, dirty_table);
	write_lost_and_found(fs_hifs);
	create_journal_dev(fs_hifs);

	ext2fs_close(fs_hifs);
	return 0;

}

static struct hifs_cache_bitmap *create_dirty_bitmap(struct hifs_cache_bitmap *dt)
{

    dt = (struct hifs_cache_bitmap *)malloc(sizeof(struct hifs_cache_bitmap));
	if (!dt) {
		return NULL;
	}


	dt->bitmap = calloc((flags.blocks/8) + 1, sizeof(uint8_t));
	if (!dt->bitmap) {
		return NULL;
	}

	// Populate anything necessary to start the bitmap. Cache's 1st entry should be sb, 2nd r_dentry
	dt->cache_block_count = flags.blocks;
	dt->cache_blocks_free = flags.blocks;
	dt->entries = 0;
	dt->cache_block_size = flags.block_size;
	dt->size = 0;
	dt->dirty = 0;

	return dt;
}

static struct hifs_cache_bitmap *create_cache_bitmap(struct hifs_cache_bitmap *ct)
{

	ct = (struct hifs_cache_bitmap *)malloc(sizeof(struct hifs_cache_bitmap));
	if (!ct) {
		return NULL;
	}
	ct->bitmap = calloc((flags.blocks/8) + 1, sizeof(uint8_t));
	if (!ct->bitmap) {
		return NULL;
	}

	// Populate anything necessary to start the bitmap. Cache's 1st entry should be sb, 2nd r_dentry
	ct->cache_block_count = flags.blocks;
	ct->cache_blocks_free = flags.blocks;
	ct->entries = 0;
	ct->cache_block_size = flags.block_size;
	ct->size = 0;
	ct->dirty = 0;

	return ct;
}

struct ext2_super_block *hifs_fill_super(struct ext2_super_block *super) {

	memset(&super, 0, sizeof(super));
	int magic = 0;
	strtoi(HIFS_MAGIC_NUM, &magic, 10);

	*super = (struct ext2_super_block) {
		.s_inode_size = sizeof(struct ext2_inode),
		.s_max_mnt_count = 20,
		.s_magic = magic,
		.s_blocks_count = flags.blocks,
		.s_inodes_count =  HIFS_MAX_CACHE_INODES,
    	.s_free_blocks_count = 1024 - 1, // All blocks except boot/superblock
    	.s_free_inodes_count = HIFS_MAX_CACHE_INODES - 1, // All inodes except the root
    	.s_first_data_block = 1, // First data block number
    	.s_blocks_per_group = 8192, // Blocks per group
    	.s_inodes_per_group = 128, // Inodes per group
    	.s_mtime = time(NULL), // Mount time
    	.s_wtime = time(NULL), // Write time
    	.s_creator_os = EXT2_OS_LINUX, // Creator OS
    	.s_rev_level = EXT2_GOOD_OLD_REV, // Revision level
		.s_def_resuid = 0, // Default user ID for reserved blocks
    	.s_def_resgid = 0, // Default group ID for reserved blocks
	};

	return super;
}

struct root_inode *hifs_create_root_inode(struct ext2_super_block *sb, struct ext2_inode *rt) {

	memset(rt, 0, sizeof(struct ext2_inode));
    rt = new_inode(sb);
    if (!rt) {
        return NULL;
    }

	memset(&rt, 0, sizeof(rt));

	*rt = (struct ext2_inode) {
		.i_mode = S_IFDIR | 0755,
		.i_uid = 0,
		.i_gid = 0,
		.i_links_count = 1,
		.i_size = 0,
		.i_atime = rt->i_ctime = rt->i_mtime = rt->i_dtime = time(NULL),
	};

	return rt;
}

static int hifs_mkfs(const char *dev_name, const char *mount_point)
{
    struct ext2_super_block *sb;
    struct ext2_inode *root_inode;
    struct ext2_dentry *root_dentry;
	struct hifs_cache_bitmap *dirty_bitmap;
	struct hifs_cache_bitmap *cache_bitmap;

	sb = (struct ext2_super_block *)malloc(sizeof(struct ext2_super_block));
	if (!sb) {
		return -1;
	}

	root_inode = (struct ext2_inode *)malloc(sizeof(struct ext2_inode));
	if (!sb) {
		return -1;
	}
	sb = hifs_fill_super(sb);

	/*
	The filesystem is using a standard ext2/ext4 style structure locally.
	Unlike other filesystems, however, local disk is only a cache.
	We have a typical inode table and blocks like any FS, but they
	point to data being cached, not a complete FS structure/files.
	*/

	// Create a cache table and cache bitmap
	cache_bitmap = create_cache_bitmap(cache_bitmap);
	dirty_bitmap = create_dirty_bitmap(dirty_bitmap);

	// Create our blank superblock first
	sb = hifs_create_root_inode(sb, root_inode);

    // Create a root inode for the filesystem
	root_inode = hifs_create_root_inode(sb, root_inode);


	// Now how does this all compare to hi_command's idea of things?
	//
	//
	//
	//////////////////////////////////////////


	// Now everythig is resolved, write out the cache device structure.
	hifs_write_sblock(sb, cache_bitmap, dirty_bitmap);

    return 0;
}

int hifs_standard_warning( char *file)
{
	char yn;
	printf("This process will destroy any existing data on the disk other than any existing boot block.\n");
	printf("Are you sure you wish to make a new filesystem on this device? [y/n] ");
	scanf(&yn);
	if (yn == 'y' || yn == 'Y') {
		printf("Creating a new filesystem on dev %s.\n", file);
		return 0;
	} else {
		printf("Aborting.\n");
		return -1;
	}
}

int main(int argc, char *argv[])
{
    int c, res;
	long blocks;
    char *unit;

	static int verbose_flag=0;
	static int force_flag=0;
	char *vol_label="HIFS Filesystem";
	char *file_name="";
	char *mount_point="";

	long long int fs_size=0;
	int block_size=HIFS_DEFAULT_BLOCK_SIZE;
	float overflow=1.3;

    while (1)
    {
        // getopt_long stores the option index here.
        int option_index = 0;

        c=getopt_long(argc, argv, "h:f:L:F:m:b:o:b:", long_options, &option_index);

        // Detect the end of the options.
        if (c==-1) break;

        switch (c)
        {

            case 'h':
                usage();
                return 0;
                break;

            case 'v':
                verbose_flag=1;
                break;

            case 'f':
                force_flag=1;
                break;

			case 'l':
                flags.lazy_init=1;
                break;

            case 'L':
                vol_label=strdup(optarg);
                break;

            case 'F':
                file_name=strdup(optarg);
                break;

            case 'm':
                mount_point=strdup(optarg);
                break;

            case 's':
                fs_size=strtol(optarg, &unit, 10);
                fs_size*=unitvalue(unit);
                break;

            case 'b':
                block_size=strtol(optarg, &unit, 10);
                block_size*=unitvalue(unit);
                break;

            case 'n':
                flags.noaction=1;
                break;

            case 'o':
                overflow=strtod(optarg, NULL);
                break;

            case '?':
                // getopt_long already printed an error message.
                break;

            default:
                abort();
        }
    }

    // check block size
    if (block_size<4096 || 2*1024*1024<block_size)
    {
        fprintf(stderr, "block size must be between 4k and 128k and specified in bytes, value entered: %d\n", block_size);
        return 1;
    }
    int bs=block_size;
    while (bs>2)
    {
        if (bs%2)
        {
            fprintf(stderr, "block size must be a power of 2, value entered: %d\n", block_size);
            return 1;
        }
        bs/=2;	
    }
	blocks = fs_size/block_size;

    // check partition size
    if (fs_size>0 && fs_size<2*block_size)
    {
        fprintf(stderr, "partition size must be a least 2 blocks wide: %lld\n", fs_size);
        return 1;
    }

	res = hifs_standard_warning(file_name);
	if (res == -1)
		exit(-1);

	// save the info for later
	flags.block_size = block_size;
	flags.blocks = blocks;
	flags.device_name = file_name;
	flags.mount_point = mount_point;
	flags.verbose = verbose_flag;
	flags.lethal_force = force_flag;
	flags.vlabel = vol_label;
	flags.size = fs_size;

	// Init FS
	hifs_mkfs(file_name, mount_point);

	return 0;

}