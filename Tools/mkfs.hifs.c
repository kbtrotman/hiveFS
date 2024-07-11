/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#include <getopt.h>
#include <linux/fs.h>

#include "../hifs.h"

const char * program_name = "mkfs.hifs";


static int verbose_flag=0;
static int force_flag=0;
char *vol_label="HIFS Filesystem";
char *file_name="";
char *mount_point="";

long long int fs_size=0;
int block_size=HIFS_DEFAULT_BLOCK_SIZE;
float overflow=1.3;
off_t ALLOCATIONGRANULARITY=65536; // max(linux.mmap.ALLOCATIONGRANULARITY, windows.mmap.ALLOCATIONGRANULARITY)

static struct option long_options[] =
{
    {"help",         no_argument,       0, 'h'},
    {"force",        no_argument,       0, 'f'},
    {"label",        no_argument,       0, 'L'},
    {"filename",     required_argument, 0, 'F'},
	{"mount",        required_argument, 0, 'm'},
    {"size",         required_argument, 0, 's'},
    {"block-size",   required_argument, 0, 'b'},
    {"overflow",     required_argument, 0, 'o'},
    {0, 0, 0, 0}
};


void usage()
{
    printf("Usage: mkfs.hifs [options] target_dir\n"
            "\n    initialize hifs superblock and cache disk\n\nOptions:\n"
            "  -h, --help            show this help message and exit\n"
            "  -f, --force           don't ask questions, just do it\n"
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

static void write_inode_tables(ext2_filsys fs, int lazy_flag, int itable_zeroed)
{
	errcode_t	retval;
	blk64_t		start = 0;
	dgrp_t		i;
	int		len = 0;
	struct ext2fs_numeric_progress_struct progress;

	ext2fs_numeric_progress_init(fs, &progress, _("Writing inode tables: "), fs->group_desc_count);

	for (i = 0; i < fs->group_desc_count; i++) {
		blk64_t blk = ext2fs_inode_table_loc(fs, i);
		int num = fs->inode_blocks_per_group;

		ext2fs_numeric_progress_update(fs, &progress, i);

		if (lazy_flag)
			num = ext2fs_div_ceil((fs->super->s_inodes_per_group - ext2fs_bg_itable_unused(fs, i)) *
                EXT2_INODE_SIZE(fs->super), EXT2_BLOCK_SIZE(fs->super));
		if (!lazy_flag || itable_zeroed) {
			/* The kernel doesn't need to zero the itable blocks */
			ext2fs_bg_flags_set(fs, i, EXT2_BG_INODE_ZEROED);
			ext2fs_group_desc_csum_set(fs, i);
		}
		if (!itable_zeroed) {
			if (len == 0) {
				start = blk;
				len = num;
				continue;
			}
			/* 'len' must not overflow 2^31 blocks for ext2fs_zero_blocks2() */
			if (start + len == blk && len + num >= len) {
				len += num;
				continue;
			}
			retval = ext2fs_zero_blocks2(fs, start, len, &start, &len);
			if (retval) {
				fprintf(stderr, _("\nCould not write %d blocks in inode table starting at %llu: %s\n"),
					len, (unsigned long long) start,
					error_message(retval));
				exit(1);
			}
			start = blk;
			len = num;
		}
		if (sync_kludge) {
			if (sync_kludge == 1)
				io_channel_flush(fs->io);
			else if ((i % sync_kludge) == 0)
				io_channel_flush(fs->io);
		}
	}
	if (len) {
		retval = ext2fs_zero_blocks2(fs, start, len, &start, &len);
		if (retval) {
			fprintf(stderr, _("\nCould not write %d blocks in inode table starting at %llu: %s\n"),
				len, (unsigned long long) start,
				error_message(retval));
			exit(1);
		}
		if (sync_kludge)
			io_channel_flush(fs->io);
	}
	ext2fs_numeric_progress_close(fs, &progress, _("done                            \n"));

	/* Reserved inodes must always have correct checksums */
	if (ext2fs_has_feature_metadata_csum(fs->super))
		write_reserved_inodes(fs);
}

static void create_root_dir(ext2_filsys fs)
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

static void create_lost_and_found(ext2_filsys fs)
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

static void reserve_inodes(ext2_filsys fs)
{
	ext2_ino_t	i;

	for (i = EXT2_ROOT_INO + 1; i < EXT2_FIRST_INODE(fs->super); i++)
		ext2fs_inode_alloc_stats2(fs, i, +1, 0);
	ext2fs_mark_ib_dirty(fs);
}

static void zap_sector(ext2_filsys fs, int sect, int nsect)
{
	char *buf;
	int retval;
	unsigned int *magic;

	buf = calloc(512, nsect);
	if (!buf) {
		printf(_("Out of memory erasing sectors %d-%d\n"), sect, sect + nsect - 1);
		exit(1);
	}

	if (sect == 0) {
		/* Check for a BSD disklabel, and don't erase it if so */
		retval = io_channel_read_blk64(fs->io, 0, -512, buf);
		if (retval)
			fprintf(stderr,
				_("Warning: could not read block 0: %s\n"),
				error_message(retval));
		else {
			magic = (unsigned int *) (buf + BSD_LABEL_OFFSET);
			if ((*magic == BSD_DISKMAGIC) || (*magic == BSD_MAGICDISK)) {
				free(buf);
				return;
			}
		}
	}

	memset(buf, 0, 512*nsect);
	io_channel_set_blksize(fs->io, 512);
	retval = io_channel_write_blk64(fs->io, sect, -512*nsect, buf);
	io_channel_set_blksize(fs->io, fs->blocksize);
	free(buf);
	if (retval)
		fprintf(stderr, _("Warning: could not erase sector %d: %s\n"),
			sect, error_message(retval));
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

int hifs_write_sblock(void)
{
	/*
	 * Wipe out the old on-disk superblock
	 */
	if (!noaction)
		zap_sector(fs, 2, 6);

#ifdef ZAP_BOOTBLOCK
	zap_sector(fs, 0, 2);
#endif

// fopen();
// lseek();
// write;   

	write_inode_tables(fs, lazy_itable_init, itable_zeroed);
	create_root_dir(fs);
	create_lost_and_found(fs);

}

static struct hifs_block_bitmap *create_cache_table(struct hifs_cache_bitmap *ct)
{
	ct->bitmap = calloc(ct->size, ct->cache_block_size);
	if (!ct->bitmap) {
		return -ENOMEM;
	}

	// Populate anything necessary to start the bitmap. Cache's 1st entry should be sb, 2nd r_dentry
	//ct->cache_block_count = ct->size/ct->cache_block_size;
	//ct->cache_blocks_free = ct->cache_block_count;
	return ct;
}

static int hifs_mkfs(int bsize, int blocks, const char *dev_name, const char *mount_point)
{
    struct super_block sb;
    struct inode *root_inode;
    struct dentry *root_dentry;
	struct hifs_block_bitmap *block_bitmap;
	struct hifs_cache_bitmap *cache_bitmap;

    // Allocate a superblock structure
    sb = get_sb_nodev(flags, mount_point, hifs_fill_super);
    if (IS_ERR(sb)) {
        return PTR_ERR(sb);
    }


    // Allocate a block bitmap

	block_bitmap = (struct hifs_block_bitmap) {
		.bitmap = NULL, 
		.size = blocks/8,
		.block_size = bsize,
		.block_count = blocks,
		.block_count_free = blocks,
	};


    // Allocate a cache inode bitmap
	cache_bitmap = (struct hifs_cache_bitmap) {
		.bitmap = NULL, 
		.size = 0,
		.entries = 0,
		.cache_block_size = 0,
		.cache_block_count = 0,
		.cache_blocks_free = 0,
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
    sb.s_root = &root_dentry;

    // Create a cache table and cache bitmap
	cache_bitmap = create_cache_table(cache_bitmap);
	if (!cache_bitmap) {
		return -ENOMEM;
	}

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

	hifs_mkfs(block_size, blocks, file_name, mount_point);

	return 0;

}