/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "../hifs.h"

const char * program_name = "mkfs.hifs";


static void usage(void)
{
	fprintf(stderr, _("Usage: %s [-c|-l filename] [-b block-size] "
	"[-J journal-options]\n"
	"\t[-G flex-group-size] [-N number-of-inodes] "
	"[-d root-directory|tarball]\n"
	"\t[-m reserved-blocks-percentage] [-o creator-os]\n"
	"\t[-L volume-label] "
	"[-M last-mounted-directory]\n\t[-O feature[,...]] "
	"[-r fs-revision] [-E extended-option[,...]]\n"
	"\t[-t fs-type] [-T usage-type ] [-U UUID] [-e errors_behavior]"
	"[-z undo_file]\n"
	"\t[-jnqvDFSV] device [blocks-count]\n"),
		program_name);
	exit(1);
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

#define BSD_DISKMAGIC   (0x82564557UL)  /* The disk magic number */
#define BSD_MAGICDISK   (0x57455682UL)  /* The disk magic number reversed */
#define BSD_LABEL_OFFSET        64

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

static int hifs_mkfs(struct file_system_type *fs_type, int flags, const char *dev_name, void *data)
{
    
    struct inode *root_inode;
    struct dentry *root_dentry;

    // Create a boot control block


    // Allocate a superblock structure
    struct super_block sb = get_sb_nodev(fs_type, flags, data, hifs_fill_super);
    if (IS_ERR(sb)) {
        return PTR_ERR(sb);
    }


    // Allocate a block bitmap

    // Allocate an inode bitmap


    // Create a root inode for the filesystem
    *root_inode = new_inode(sb);
    if (!root_inode) {
        return -ENOMEM;
    }

    // Initialize the root inode
    root_inode->i_ino = 1;
    root_inode->i_sb = sb;
    root_inode->i_op = &hifs_inode_operations;
    root_inode->i_fop = &hifs_dir_operations;
    root_inode->i_mode = S_IFDIR | 0755;

    // Create a dentry for the root directory
    *root_dentry = d_make_root(root_inode);
    if (!root_dentry) {
        iput(root_inode);
        return -ENOMEM;
    }

    // Set the root dentry for the superblock
    sb->s_root = root_dentry;
    */

    // Create a cache table and cache bitmap



    return 0;
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