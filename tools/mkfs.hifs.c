/**
 * HiveFS mkfs utility
 *
 * Creates a basic on-disk layout for HiveFS cached local FS and pairs with remote hive.
 * The tool reserves the metadata region, initialises the superblock, cache
 * bitmaps, inode tables and root directory block using the structures shared
 * with the kernel module.
 */

#include "tools.h"

#include <errno.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const struct option long_options[] = {
	{"help",     no_argument,       NULL, 'h'},
	{"force",    no_argument,       NULL, 'f'},
	{"verbose",  no_argument,       NULL, 'v'},
	{"lazy_init",no_argument,       NULL, 'l'},
	{"label",    required_argument, NULL, 'L'},
	{"filename", required_argument, NULL, 'F'},
	{"mount",    required_argument, NULL, 'm'},
	{"size",     required_argument, NULL, 's'},
	{"noaction", no_argument,       NULL, 'n'},
	{0, 0, 0, 0}
};

static void usage(void)
{
	printf(
		"HiveFS mkfs - initialise a HiveFS cache image\n\n"
		"Usage: mkfs.hifs [options]\n\n"
		"Options:\n"
		"  -h, --help            Show this message and exit\n"
		"  -f, --force           Skip confirmation prompts\n"
		"  -v, --verbose         Verbose output\n"
		"  -l, --lazy_init       Skip aggressively zeroing metadata blocks\n"
		"  -L, --label=LABEL     Set volume label (max 15 chars)\n"
		"  -F, --filename=PATH   Target block device or regular file\n"
		"  -m, --mount=PATH      Default mount point stored in superblock\n"
		"  -s, --size=SIZE       Size of filesystem image (accepts K/M/G suffix)\n"
		"  -n, --noaction        Dry-run; do not modify the target\n"
		"\nExample:\n"
		"  mkfs.hifs -F /dev/sdb3 -s 20G -m /mnt/hive\n"
	);
}

static uint64_t parse_scaled(const char *arg)
{
	char *end = NULL;
	errno = 0;
	uint64_t value = strtoull(arg, &end, 10);
	if (errno || value == 0)
		return 0;

	if (!end || *end == '\0')
		return value;

	switch (*end) {
	case 'k': case 'K': return value * 1024ULL;
	case 'm': case 'M': return value * 1024ULL * 1024ULL;
	case 'g': case 'G': return value * 1024ULL * 1024ULL * 1024ULL;
	case 't': case 'T': return value * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
	default:
		return 0;
	}
}

static uint32_t to_le32(uint32_t v) { return v; }
static uint16_t to_le16(uint16_t v) { return v; }

static int ensure_size(int fd, uint64_t size)
{
	if (size == 0)
		return 0;

	if (ftruncate(fd, size) < 0)
		return -errno;
	return 0;
}

static int write_region(int fd, off_t offset, const void *buf, size_t len)
{
	const uint8_t *ptr = buf;
	size_t written = 0;

	while (written < len) {
		ssize_t ret = pwrite(fd, ptr + written, len - written, offset + written);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			return -errno;
		}
		written += (size_t)ret;
	}
	return 0;
}

static void bitmap_mark(struct hifs_cache_bitmap *bmp, uint64_t block)
{
	const uint64_t byte = block / 8;
	const uint8_t bit = block % 8;

	if (byte >= bmp->size)
		return;

	if (!(bmp->bitmap[byte] & (1u << bit))) {
		bmp->bitmap[byte] |= (1u << bit);
		if (bmp->cache_blocks_free)
			bmp->cache_blocks_free--;
		bmp->entries++;
	}
}

static struct hifs_cache_bitmap *bitmap_create(uint64_t blocks, uint32_t block_size)
{
	struct hifs_cache_bitmap *bmp = calloc(1, sizeof(*bmp));
	if (!bmp)
		return NULL;

	bmp->size = (blocks + 7) / 8;
	bmp->bitmap = calloc(bmp->size, sizeof(uint8_t));
	if (!bmp->bitmap) {
		free(bmp);
		return NULL;
	}
	bmp->cache_block_size = block_size;
	bmp->cache_block_count = blocks;
	bmp->cache_blocks_free = blocks;
	return bmp;
}

static void bitmap_destroy(struct hifs_cache_bitmap *bmp)
{
	if (!bmp)
		return;
	free(bmp->bitmap);
	free(bmp);
}

static int write_bitmap_region(int fd, off_t offset, const struct hifs_cache_bitmap *bmp, uint32_t block_size)
{
	size_t aligned = (bmp->size + block_size - 1) / block_size * block_size;
	uint8_t *buffer = calloc(1, aligned);
	if (!buffer)
		return -ENOMEM;

	memcpy(buffer, bmp->bitmap, bmp->size);
	int ret = write_region(fd, offset, buffer, aligned);
	free(buffer);
	return ret;
}

static uint32_t log_block_size(uint32_t block_size)
{
	uint32_t log = 0;
	uint32_t size = block_size >> 10; /* expressed relative to 1024 */
	while (size > 1) {
		size >>= 1;
		log++;
	}
	return log;
}

static void init_superblock(struct hifs_superblock *sb,
			    uint64_t blocks,
			    uint32_t block_size,
			    const char *label,
			    const char *mount_point)
{
	memset(sb, 0, sizeof(*sb));

	time_t now = time(NULL);

	sb->s_inodes_count      = to_le32(HIFS_MAX_CACHE_INODES);
	sb->s_blocks_count      = to_le32((uint32_t)blocks);
	sb->s_r_blocks_count    = 0;
	sb->s_free_blocks_count = to_le32((uint32_t)blocks);
	sb->s_free_inodes_count = to_le32(HIFS_MAX_CACHE_INODES - 1);
	sb->s_first_data_block  = to_le32(HIFS_CACHE_SPACE_START / block_size);
	sb->s_log_block_size    = to_le32(log_block_size(block_size));
	sb->s_blocks_per_group  = to_le32(blocks);
	sb->s_inodes_per_group  = to_le32(HIFS_MAX_CACHE_INODES);
	sb->s_mtime             = to_le32((uint32_t)now);
	sb->s_wtime             = to_le32((uint32_t)now);
	sb->s_mnt_count         = to_le16(0);
	sb->s_max_mnt_count     = to_le16(20);
	sb->s_magic             = to_le16((uint16_t)HIFS_MAGIC_NUM);
	sb->s_rev_level         = to_le32(HIFS_LAYOUT_VER);
	sb->s_inode_size        = to_le16(sizeof(struct hifs_inode));
	sb->s_first_ino         = to_le32(HIFS_ROOT_INODE);
	sb->s_mkfs_time         = to_le32((uint32_t)now);

	if (label)
		strncpy(sb->s_volume_name, label, sizeof(sb->s_volume_name));
	if (mount_point)
		strncpy(sb->s_last_mounted, mount_point, sizeof(sb->s_last_mounted));
}

static void init_root_inode(struct hifs_inode *inode, uint32_t block_size)
{
	memset(inode, 0, sizeof(*inode));
	const uint32_t dir_block = HIFS_ROOT_DENTRY_OFFSET / block_size;
	time_t now = time(NULL);

	inode->i_version = HIFS_LAYOUT_VER;
	inode->i_flags = 0;
	inode->i_mode = S_IFDIR | 0755;
	inode->i_ino = HIFS_ROOT_INODE;
	inode->i_uid = 0;
	inode->i_gid = 0;
	inode->i_hrd_lnk = 2;
	inode->i_atime = inode->i_mtime = inode->i_ctime = (uint32_t)now;
	inode->i_size = block_size;
	inode->i_blocks = 1;
	inode->i_bytes = block_size;
	inode->i_addrb[0] = dir_block;
	inode->i_addre[0] = dir_block + 1;
	inode->i_links = 2;
	strncpy(inode->i_name, "/", sizeof(inode->i_name));
}

static int write_inode_table(int fd, off_t offset, const struct hifs_inode *inode, uint32_t block_size)
{
	uint8_t *block = calloc(1, block_size);
	if (!block)
		return -ENOMEM;

	memcpy(block, inode, sizeof(*inode));
	int ret = write_region(fd, offset, block, block_size);
	free(block);
	return ret;
}

static int write_root_directory(int fd, uint32_t block_size)
{
	size_t count = block_size / sizeof(struct hifs_dir_entry);
	struct hifs_dir_entry *entries = calloc(count, sizeof(struct hifs_dir_entry));
	if (!entries)
		return -ENOMEM;

	for (size_t i = 0; i < count; ++i)
		entries[i].inode_nr = HIFS_EMPTY_ENTRY;

	entries[0].inode_nr = HIFS_ROOT_INODE;
	entries[0].name_len = 1;
	memcpy(entries[0].name, ".", 1);

	entries[1].inode_nr = HIFS_ROOT_INODE;
	entries[1].name_len = 2;
	memcpy(entries[1].name, "..", 2);

	int ret = write_region(fd, HIFS_ROOT_DENTRY_OFFSET, entries, block_size);
	free(entries);
	return ret;
}

static int zero_region(int fd, off_t start, size_t length, uint32_t block_size)
{
	const size_t chunk = block_size;
	uint8_t *buf = calloc(1, chunk);
	if (!buf)
		return -ENOMEM;

	size_t remaining = length;
	off_t offset = start;
	while (remaining) {
		size_t step = remaining < chunk ? remaining : chunk;
		int ret = write_region(fd, offset, buf, step);
		if (ret) {
			free(buf);
			return ret;
		}
		offset += step;
		remaining -= step;
	}

	free(buf);
	return 0;
}

static int confirm_overwrite(const char *path)
{
	if (flags.lethal_force)
		return 0;

	printf("This will destroy existing data on %s. Proceed? [y/N] ", path);
	fflush(stdout);
	char response = 0;
	if (scanf(" %c", &response) != 1)
		return -EINVAL;
	if (response != 'y' && response != 'Y')
		return -ECANCELED;
	return 0;
}

static int create_filesystem(const char *path,
			     uint64_t size,
			     uint32_t block_size,
			     const char *label,
			     const char *mount_point)
{
	int fd = open(path, O_RDWR | O_CREAT, 0644);
	if (fd < 0)
		return -errno;

	if (!flags.noaction) {
		int ret = confirm_overwrite(path);
		if (ret) {
			close(fd);
			return ret;
		}
	}

	int ret = ensure_size(fd, size);
	if (ret) {
		close(fd);
		return ret;
	}

	uint64_t blocks = size / block_size;
	if (blocks < 8) {
		close(fd);
		return -EINVAL;
	}

	struct hifs_cache_bitmap *cache_bmp = bitmap_create(blocks, block_size);
	struct hifs_cache_bitmap *dirty_bmp = bitmap_create(blocks, block_size);
	if (!cache_bmp || !dirty_bmp) {
		ret = -ENOMEM;
		goto out_close;
	}

	const uint64_t reserved_blocks[] = {
		HIFS_BOOT_OFFSET / block_size,
		HIFS_INODE_BITMAP_OFFSET / block_size,
		HIFS_INODE_TABLE_OFFSET / block_size,
		HIFS_INODE_TABLE2_OFFSET / block_size,
		HIFS_DIRTY_TABLE_OFFSET / block_size,
		HIFS_ROOT_DENTRY_OFFSET / block_size
	};

	for (size_t i = 0; i < ARRAY_SIZE(reserved_blocks); ++i) {
		bitmap_mark(cache_bmp, reserved_blocks[i]);
		bitmap_mark(dirty_bmp, reserved_blocks[i]);
	}

	struct hifs_superblock sb;
	init_superblock(&sb, blocks, block_size, label, mount_point);

	struct hifs_inode root_inode;
	init_root_inode(&root_inode, block_size);

	if (!flags.lazy_init && !flags.noaction) {
		ret = zero_region(fd, 0, HIFS_CACHE_SPACE_START + block_size, block_size);
		if (ret)
			goto out_close;
	}

	if (!flags.noaction) {
		ret = write_region(fd, HIFS_SUPER_OFFSET, &sb, sizeof(sb));
		if (ret)
			goto out_close;

		ret = write_bitmap_region(fd, HIFS_INODE_BITMAP_OFFSET, cache_bmp, block_size);
		if (ret)
			goto out_close;

		ret = write_bitmap_region(fd, HIFS_DIRTY_TABLE_OFFSET, dirty_bmp, block_size);
		if (ret)
			goto out_close;

		ret = write_inode_table(fd, HIFS_INODE_TABLE_OFFSET, &root_inode, block_size);
		if (ret)
			goto out_close;

		ret = write_inode_table(fd, HIFS_INODE_TABLE2_OFFSET, &root_inode, block_size);
		if (ret)
			goto out_close;

		ret = write_root_directory(fd, block_size);
		if (ret)
			goto out_close;
	}

	if (flags.verbose)
		printf("HiveFS image written to %s (%" PRIu64 " blocks).\n", path, blocks);

out_close:
	bitmap_destroy(cache_bmp);
	bitmap_destroy(dirty_bmp);
	close(fd);
	return ret;
}

int main(int argc, char *argv[])
{
	memset(&flags, 0, sizeof(flags));
	flags.block_size = HIFS_DEFAULT_BLOCK_SIZE;

	char *label = "HiveFS";
	char *mount_point = "/";
	char *device = NULL;
	uint64_t image_size = 0;

	while (1) {
		int option_index = 0;
		int c = getopt_long(argc, argv, "hfvlL:F:m:s:n", long_options, &option_index);
		if (c == -1)
			break;

		switch (c) {
		case 'h':
			usage();
			return 0;
		case 'f':
			flags.lethal_force = true;
			break;
		case 'v':
			flags.verbose = 1;
			break;
		case 'l':
			flags.lazy_init = true;
			break;
		case 'L':
			label = optarg;
			break;
		case 'F':
			device = optarg;
			break;
		case 'm':
			mount_point = optarg;
			break;
		case 's':
			image_size = parse_scaled(optarg);
			break;
		case 'n':
			flags.noaction = true;
			break;
		default:
			usage();
			return 1;
		}
	}

	if (!device) {
		fprintf(stderr, "No target device or file specified (use -F)\n");
		return 1;
	}

	if (image_size == 0) {
		struct stat st;
		if (stat(device, &st) == 0 && S_ISREG(st.st_mode))
			image_size = st.st_size;
		if (image_size == 0) {
			fprintf(stderr, "Unable to determine image size; use -s SIZE\n");
			return 1;
		}
	}

	int ret = create_filesystem(device, image_size, flags.block_size, label, mount_point);
	if (ret) {
		if (ret == -ECANCELED)
			return 1;
		errno = -ret;
		perror("mkfs.hifs");
		return 1;
	}

	return 0;
}
