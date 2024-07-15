/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#include "tools.h"

#include "../hifs.h"

struct command_line_flags flags;
const char * program_name = "mkfs.hifs";
int itable_zeroed = 0;

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
    {"block-size",   required_argument, 0, 'b'},
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
	struct ext2fs_numeric_progress_struct progress;
	errcode_t		retval;
	char			*buf;
	blk64_t			blk, err_blk;
	int			c, count, err_count;
	struct ext2fs_journal_params	jparams;

	retval = ext2fs_get_journal_params(&jparams, fs);
	if (retval) {
		com_err("create_journal_dev", retval, "%s",
			_("while splitting the journal size"));
		exit(1);
	}

	retval = ext2fs_create_journal_superblock2(fs, &jparams, 0, &buf);
	if (retval) {
		com_err("create_journal_dev", retval, "%s",
			_("while initializing journal superblock"));
		exit(1);
	}

	if (journal_flags & EXT2_MKJOURNAL_LAZYINIT)
		goto write_superblock;

	ext2fs_numeric_progress_init(fs, &progress, _("Zeroing journal device: "), ext2fs_blocks_count(fs->super));
	blk = 0;
	count = ext2fs_blocks_count(fs->super);
	while (count > 0) {
		if (count > 1024)
			c = 1024;
		else
			c = count;
		retval = ext2fs_zero_blocks2(fs, blk, c, &err_blk, &err_count);
		if (retval) {
			com_err("create_journal_dev", retval, ("while zeroing journal device (block %llu, count %d)"),
				(unsigned long long) err_blk, err_count);
			exit(1);
		}
		blk += c;
		count -= c;
		ext2fs_numeric_progress_update(fs, &progress, blk);
	}

	ext2fs_numeric_progress_close(fs, &progress, NULL);
write_superblock:
	retval = io_channel_write_blk64(fs->io,
					fs->super->s_first_data_block+1,
					1, buf);
	(void) ext2fs_free_mem(&buf);
	if (retval) {
		com_err("create_journal_dev", retval, "%s", _("while writing journal superblock"));
		exit(1);
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
		com_err("ext2fs_mkdir", retval, "%s",
			_("while creating /lost+found"));
		exit(1);
	}

	retval = ext2fs_lookup(fs, EXT2_ROOT_INO, name, strlen(name), 0, &ino);
	if (retval) {
		com_err("ext2_lookup", retval, "%s",
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
			com_err("ext2fs_expand_dir", retval, "%s",
				_("while expanding /lost+found"));
			exit(1);
		}
	}
}

static void write_root_dir(ext2_filsys fs)
{
	errcode_t		retval;
	struct ext2_inode	inode;
	int need_inode_change;

	retval = ext2fs_mkdir(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, 0);
	if (retval) {
		com_err("ext2fs_mkdir", retval, "%s",
			_("while creating root dir"));
		exit(1);
	}

	need_inode_change = (int)(root_uid != 0 || root_gid != 0 || root_perms != (mode_t)-1);

	if (need_inode_change) {
		retval = ext2fs_read_inode(fs, EXT2_ROOT_INO, &inode);
		if (retval) {
			com_err("ext2fs_read_inode", retval, "%s",
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
			com_err("ext2fs_write_inode", retval, "%s",
				_("while setting root inode ownership"));
			exit(1);
		}
	}
}

static void write_cache_table(ext2_filsys fs, struct hifs_cache_bitmap *cache_table, struct hifs_cache_bitmap *dirty_table)
{
	errcode_t	retval;
	blk64_t		start = 0;
	dgrp_t		i;
	int		len = 0;
	struct ext2fs_numeric_progress_struct progress;

	ext2fs_numeric_progress_init(fs, &progress, _("Writing inode tables: "), fs->group_desc_count);

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
			ext2fs_numeric_progress_update(fs, &progress, i);
		}
	}

	// Then write the inode bitmap, inode tables and dirty bitmap.
	// Start with a zeroed bitmap, then write it.
	io_channel_set_blksize(fs->io, flags.block_size);
	retval = io_channel_write_blk64(fs->io, 1, 1, cache_table->bitmap);	
	retval = io_channel_write_blk64(fs->io, 4, 1, dirty_table->bitmap);
	if (retval) {
		com_err("write_cache_table", retval, "%s", _("while writing cache bitmaps"));
		exit(1);
	}

	// Next zero out both tables and write those. (IE: tables with no actual data yet.)
		// This one is slightly more difficult. There are a few questions:
			// 1. Percentage of inodes to blocks to allow? Configurable?
			// 2. Size of each inode determines # of inodes per block.
			// 3. Table keeps tract of each inode location/ block location.
			// 4. Initially, we need 0 inodes and 0 blocks, and only place holder structs.
			// 5. Once determined, place holders are written out.



	ext2fs_numeric_progress_close(fs, &progress, _("done                            \n"));
}

static void zap_sector(ext2_filsys fs, int sect, int nsect)
{
	char *buf;
	int retval;
	unsigned int *magic;

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
		fprintf(stderr, _("Warning: could not erase sector %d: %s\n"), sect, error_message(retval));
}

int hifs_write_sblock(struct super_block *sb, struct hifs_cache_bitmap *cache_table, struct hifs_cache_bitmap *dirty_table)
{
	int fd;
	ext2_filsys fs_hifs;
	errcode_t err;

	// First let's populate our new sb before we do any device IO.
	hifs_fill_super(&sb);

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
        return -1;
    }

	// write super;   
    // Write the superblock to the device
    if (write(fd, sb, sizeof(struct super_block)) != sizeof(struct super_block)) {
        perror("write");
        return -1;
    }

	write_cache_table(fs_hifs, cache_table, dirty_table);
	write_root_dir(sb);
	write_lost_and_found(fs_hifs);

}

static struct hifs_cache_bitmap *create_dirty_table(struct hifs_cache_bitmap *dt)
{
	dt->bitmap = calloc((flags.blocks/8) + 1, sizeof(uint8_t));
	if (!dt->bitmap) {
		return -ENOMEM;
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

static struct hifs_cache_bitmap *create_cache_table(struct hifs_cache_bitmap *ct)
{
	ct->bitmap = calloc((flags.blocks/8) + 1, sizeof(uint8_t));
	if (!ct->bitmap) {
		return -ENOMEM;
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

static int hifs_mkfs(const char *dev_name, const char *mount_point)
{
    struct super_block *sb;
    struct inode *root_inode;
    struct dentry *root_dentry;
	struct hifs_cache_bitmap *dirty_bitmap;
	struct hifs_cache_bitmap *cache_bitmap;


    // Allocate a dirty bitmap
	*dirty_bitmap = (struct hifs_cache_bitmap) {
		.bitmap = NULL, 
		.size = flags.size,
		.entries = 0,
		.cache_block_size = flags.block_size,
		.cache_block_count = flags.blocks,
		.cache_blocks_free = flags.blocks,
	};


    // Allocate a cache inode bitmap
	*cache_bitmap = (struct hifs_cache_bitmap) {
		.bitmap = NULL, 
		.size = flags.size,
		.entries = 0,
		.cache_block_size = flags.block_size,
		.cache_block_count = flags.blocks,
		.cache_blocks_free = flags.blocks,
	};

    // Create a root inode for the filesystem
    root_inode = new_inode(sb);
    if (!root_inode) {
        return -ENOMEM;
    }

    // Initialize the root inode
    root_inode->i_ino = 1;
    root_inode->i_sb = &sb;
    root_inode->i_op = &hifs_inode_operations;
    root_inode->i_fop = &hifs_dir_operations;
    root_inode->i_mode = S_IFDIR | 0755;

    // Create a dentry for the root directory
    root_dentry = d_make_root(root_inode);
    if (!root_dentry) {
        iput(root_inode);
        return -ENOMEM;
    }

    // Set the root dentry for the superblock
    sb->s_root = &root_dentry;

    // Create a cache table and cache bitmap
	cache_bitmap = create_cache_table(cache_bitmap);
	if (!cache_bitmap) {
		return -ENOMEM;
	}

	dirty_bitmap = create_dirty_table(dirty_bitmap);
	if (!dirty_bitmap) {
		return -ENOMEM;
	}
	// Now how does this all compare to hi_command's idea of things?

	// Now everythig is resolved, write out the cache device structure.
	hifs_write_sblock(sb, cache_bitmap, dirty_bitmap);

    return 0;
}

int hifs_standard_warning( char *file)
{
	char c;
	printf("This process will destroy any existing data on the disk other than the boot block.\n");
	printf("Are you sure you wish to make a new filesystem on this device? [y/n] ");
	scanf("%c");
	if (c == 'y' || c == 'Y') {
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