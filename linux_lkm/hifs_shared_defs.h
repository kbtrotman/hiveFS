/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#pragma once

#ifdef __KERNEL__
#include <linux/jiffies.h>
#include <linux/types.h>
#define GET_TIME() (jiffies * 1000 / HZ)
#include <linux/list.h>
#include <linux/ioctl.h>
#else
#include <time.h>
#include <stdint.h>
#include <stdio.h>
#include <syslog.h>
#include <stdarg.h>
#define GET_TIME() (clock() * 1000 / CLOCKS_PER_SEC)
#endif

/* Basic metadata shared between hive components */
struct superblock {
	uint64_t volume_id;
	uint32_t s_magic;
	uint32_t s_blocksize;
	uint32_t s_blocksize_bits;
	uint64_t s_blocks_count;
	uint64_t s_free_blocks;
	uint64_t s_inodes_count;
	uint64_t s_free_inodes;
};

struct machine {
	char *serial;
	char *name;
	long host_id;
	char *os_name;
	char *os_version;
	char *create_time;
};


#ifdef __KERNEL__
#define HIFS_LOG(level, fmt, ...) printk(level "hivefs: " fmt "\n", ##__VA_ARGS__)
#define HIFS_LOG_FMT(fmt) "%s:%d:%s: " fmt
#define HIFS_LOG_ARGS __FILE__, __LINE__, __func__
#define hifs_emerg(fmt, ...)   HIFS_LOG(KERN_EMERG,   HIFS_LOG_FMT(fmt), HIFS_LOG_ARGS, ##__VA_ARGS__)
#define hifs_alert(fmt, ...)   HIFS_LOG(KERN_ALERT,   HIFS_LOG_FMT(fmt), HIFS_LOG_ARGS, ##__VA_ARGS__)
#define hifs_crit(fmt, ...)    HIFS_LOG(KERN_CRIT,    HIFS_LOG_FMT(fmt), HIFS_LOG_ARGS, ##__VA_ARGS__)
#define hifs_err(fmt, ...)     HIFS_LOG(KERN_ERR,     HIFS_LOG_FMT(fmt), HIFS_LOG_ARGS, ##__VA_ARGS__)
#define hifs_warning(fmt, ...) HIFS_LOG(KERN_WARNING, HIFS_LOG_FMT(fmt), HIFS_LOG_ARGS, ##__VA_ARGS__)
#define hifs_notice(fmt, ...)  HIFS_LOG(KERN_NOTICE,  HIFS_LOG_FMT(fmt), HIFS_LOG_ARGS, ##__VA_ARGS__)
#define hifs_info(fmt, ...)    HIFS_LOG(KERN_INFO,    HIFS_LOG_FMT(fmt), HIFS_LOG_ARGS, ##__VA_ARGS__)
#define hifs_debug(fmt, ...)   HIFS_LOG(KERN_DEBUG,   HIFS_LOG_FMT(fmt), HIFS_LOG_ARGS, ##__VA_ARGS__)
#else
static inline void hifs_user_log(int level, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	FILE *stream = (level <= LOG_ERR) ? stderr : stdout;
	vfprintf(stream, fmt, ap);
	fputc('\n', stream);
	va_end(ap);

	va_start(ap, fmt);
	vsyslog(level, fmt, ap);
	va_end(ap);
}
#define HIFS_LOG(level, fmt, ...) hifs_user_log(level, "hivefs: " fmt, ##__VA_ARGS__)
#define hifs_emerg(fmt, ...)   HIFS_LOG(LOG_EMERG,   fmt, ##__VA_ARGS__)
#define hifs_alert(fmt, ...)   HIFS_LOG(LOG_ALERT,   fmt, ##__VA_ARGS__)
#define hifs_crit(fmt, ...)    HIFS_LOG(LOG_CRIT,    fmt, ##__VA_ARGS__)
#define hifs_err(fmt, ...)     HIFS_LOG(LOG_ERR,     fmt, ##__VA_ARGS__)
#define hifs_warning(fmt, ...) HIFS_LOG(LOG_WARNING, fmt, ##__VA_ARGS__)
#define hifs_notice(fmt, ...)  HIFS_LOG(LOG_NOTICE,  fmt, ##__VA_ARGS__)
#define hifs_info(fmt, ...)    HIFS_LOG(LOG_INFO,    fmt, ##__VA_ARGS__)
#define hifs_debug(fmt, ...)   HIFS_LOG(LOG_DEBUG,   fmt, ##__VA_ARGS__)
#endif // __KERNEL__

/***********************************
 * Shared Hi_command & guard defs
 ***********************************/
#define HIFS_GUARD_HOST "127.0.0.1"
#define HIFS_GUARD_PORT_STR "7070"

/******************************
 * Base FS Information
 ******************************/
/* FS Name */
#define HIFS_NAME		"hivefs"
#define HIFS_VERSION	"0.1"
/* FS Layout version/schema */
#define HIFS_LAYOUT_VER		1

/* Debug / temporary feature flags */
#ifndef HIFS_DEBUG_DISABLE_EC
#define HIFS_DEBUG_DISABLE_EC 1
#endif
#ifndef HIFS_DEBUG_DISABLE_STRIPES
#define HIFS_DEBUG_DISABLE_STRIPES 1
#endif

/* Erasure coding profile shared with hive_guard */
#define HIFS_EC_K         4
#define HIFS_EC_M         2
#define HIFS_EC_W         6
#define HIFS_EC_CHECKSUM  CHKSUM_CRC32
#define HIFS_EC_STRIPES  (HIFS_EC_K + HIFS_EC_M)

#ifndef HIFS_GUARD_HOST
#define HIFS_GUARD_HOST "127.0.0.1"
#endif
#ifndef HIFS_GUARD_PORT_STR
#define HIFS_GUARD_PORT_STR "7070"
#endif

/******************************
 * Queue Management Structures
 ******************************/
// These definitions are for 3 shared memory areas (queues).
// The kernel module and hi_command user-space program
// shared these memory areas to send and receive data.
#define HIFS_DEFAULT_BLOCK_SIZE 4096
#define HIFS_BUFFER_SIZE 4096
#define HIFS_MAX_CMD_SIZE 50 // MAX_CMD_SIZE is the maximum size of a command
#define HIFS_MAX_NAME_SIZE 256 //MAX NAME SIZE is the maximum size of a file name, dir name, or other name in FS.

#define HIFS_COMM_DEVICE_NAME "hivefs"
#define HIFS_CMD_RING_CAPACITY 128
#define HIFS_DATA_RING_CAPACITY 128
#define HIFS_DATA_MAX  5120 // Max data size in a data frame

#define HIFS_IOCTL_MAGIC      'H'
#define HIFS_IOCTL_CMD_DEQUEUE    _IOWR(HIFS_IOCTL_MAGIC, 0, struct hifs_cmds)
#define HIFS_IOCTL_DATA_DEQUEUE   _IOWR(HIFS_IOCTL_MAGIC, 1, struct hifs_data_frame)
#define HIFS_IOCTL_STATUS         _IOR(HIFS_IOCTL_MAGIC,  2, struct hifs_comm_status)
#define HIFS_IOCTL_CMD_ENQUEUE    _IOW (HIFS_IOCTL_MAGIC, 3, struct hifs_cmds)
#define HIFS_IOCTL_DATA_ENQUEUE   _IOW (HIFS_IOCTL_MAGIC, 4, struct hifs_data_frame)
#define HIFS_IOCTL_CACHE_STATUS   _IOR(HIFS_IOCTL_MAGIC,  5, struct hifs_cache_status)

/******************************
 * Queue Comms Protocol
 ******************************/
// Our protocol for comms using the comms queues.
// these values are set in the atomic variable.

#define HIFS_Q_PROTO_VERSION  1          // Version of the protocol
#define HIFS_Q_PROTO_UNUSED 0            // Queue or file(mem device) is not in use at all
#define HIFS_Q_PROTO_KERNEL_UP 1         // Kernel connection is UP.
#define HIFS_Q_PROTO_USER_UP 1           // User connection is UP.

#define HIFS_Q_PROTO_NUM_CMDS        9 //Max ENUM value of any command (same as max number of commands that exist).
#define HIFS_Q_PROTO_CMD_FLUSH          "cache_flush_all"
#define HIFS_Q_PROTO_CMD_BLOCK_RECV     "block_recv"
#define HIFS_Q_PROTO_CMD_BLOCK_SEND     "block_send"
#define HIFS_Q_PROTO_CMD_INODE_RECV     "inode_recv"
#define HIFS_Q_PROTO_CMD_INODE_SEND     "inode_send"
#define HIFS_Q_PROTO_CMD_FILE_RECV      "file_recv"
#define HIFS_Q_PROTO_CMD_FILE_SEND      "file_send"
#define HIFS_Q_PROTO_CMD_ENGINE_VERS    "engine_version"
#define HIFS_Q_PROTO_CMD_TEST           "test_cmd"
#define HIFS_Q_PROTO_CMD_LINK_UP        "link_up"
#define HIFS_Q_PROTO_CMD_LINK_INIT      "link_init"
#define HIFS_Q_PROTO_CMD_LINK_READY     "link_ready"
#define HIFS_Q_PROTO_CMD_LINK_DOWN      "link_down"
#define HIFS_Q_PROTO_CMD_CACHELESS_MODE "cacheless_mode"
/* Superblock exchange */
#define HIFS_Q_PROTO_CMD_SB_SEND        "sb_send"
#define HIFS_Q_PROTO_CMD_SB_RECV        "sb_recv"
#define HIFS_Q_PROTO_CMD_ROOT_SEND      "root_send"
#define HIFS_Q_PROTO_CMD_ROOT_RECV      "root_recv"
#define HIFS_Q_PROTO_CMD_DENTRY_SEND    "dentry_send"
#define HIFS_Q_PROTO_CMD_DENTRY_RECV    "dentry_recv"

#define HIFS_DENTRY_MSGF_REQUEST  0x00000001


enum hifs_module{HIFS_COMM_PROGRAM_KERN_MOD, HIFS_COMM_PROGRAM_USER_HICOMM};
enum hifs_queue_direction{HIFS_COMM_TO_USER, HIFS_COMM_FROM_USER};

extern struct pollfd *cmd_pfd;
extern struct pollfd *inode_pfd;
extern struct pollfd *block_pfd;

struct hifs_blocks {
	int block_size;
	int count;
	char block[HIFS_DEFAULT_BLOCK_SIZE];
};

struct hifs_cmds {
    int count;
    char cmd[HIFS_MAX_CMD_SIZE];
};

/* Generic data frame for variable-length payloads (up to HIFS_DATA_MAX). */
struct hifs_data_frame {
#ifdef __KERNEL__
    __u32 len;
    __u8  data[HIFS_DATA_MAX];
#else
    uint32_t len;
    uint8_t  data[HIFS_DATA_MAX];
#endif
};
struct hifs_comm_status {
#ifdef __KERNEL__
	__u32 cmd_available;
	__u32 inode_available;
	__u32 cmd_pending;
	__u32 inode_pending;
#else
	uint32_t cmd_available;
	uint32_t inode_available;
	uint32_t cmd_pending;
	uint32_t inode_pending;
#endif
};

struct hifs_cache_status {
#ifdef __KERNEL__
	__u64 block_bytes_total;
	__u64 block_bytes_used;
	__u32 block_unit_bytes;
	__u32 block_fifo_capacity;
	__u32 block_fifo_count;

	__u64 dirent_bytes_total;
	__u64 dirent_bytes_used;
	__u32 dirent_unit_bytes;
	__u32 dirent_fifo_capacity;
	__u32 dirent_fifo_count;

	__u64 inode_bytes_total;
	__u64 inode_bytes_used;
	__u32 inode_unit_bytes;
	__u32 inode_fifo_capacity;
	__u32 inode_fifo_count;
#else
	uint64_t block_bytes_total;
	uint64_t block_bytes_used;
	uint32_t block_unit_bytes;
	uint32_t block_fifo_capacity;
	uint32_t block_fifo_count;

	uint64_t dirent_bytes_total;
	uint64_t dirent_bytes_used;
	uint32_t dirent_unit_bytes;
	uint32_t dirent_fifo_capacity;
	uint32_t dirent_fifo_count;

	uint64_t inode_bytes_total;
	uint64_t inode_bytes_used;
	uint32_t inode_unit_bytes;
	uint32_t inode_fifo_capacity;
	uint32_t inode_fifo_count;
#endif
};

enum hifs_link_state { HIFS_COMM_LINK_DOWN, HIFS_COMM_LINK_PARTIAL, HIFS_COMM_LINK_UP };
struct hifs_link {
    enum hifs_link_state state;
    int last_check;
    int last_state;
    long int clockstart;
	enum hifs_link_state remote_state;	
};
extern struct hifs_link hifs_kern_link;
extern struct hifs_link hifs_user_link;
/******************************
 * END Queue Management Structures
 ******************************/

#define HIFS_IO_WAIT_DEFAULT_MS 1000U
#define HIFS_IO_WAIT_MAX_MS     (5U * 60U * 1000U) /* 5 minutes cap */
#define HIFS_CACHE_FLUSH_INTERVAL_DEFAULT_MS 5000U        /* 5 seconds */
#define HIFS_CACHE_FLUSH_INTERVAL_MAX_MS     (60U * 60U * 1000U) /* 1 hour cap */


/***********************
 * Hive FS Structures
 ***********************/

/* Filesystem Settings */
extern struct 
{
	char mount_point[HIFS_MAX_NAME_SIZE];
	int block_size;
} settings;

/* FS constants */
#define HIFS_MAGIC_NUM		    0x1fa7d0d0
#define HIFS_EMPTY_ENTRY		0xdeeddeed
#define HIFS_MAX_CACHE_INODES   65536

#define HIFS_NAME_LEN		     255
#define HIFS_INODE_MSIZE		 4
#define HIFS_INODE_TSIZE		 4
#define HIFS_BLOCK_HASH_SIZE     16
#define HIFS_MAX_BLOCK_HASHES    128

enum hifs_hash_algorithm {
	HIFS_HASH_ALGO_NONE = 0,
	HIFS_HASH_ALGO_SHA256 = 1,
	HIFS_HASH_ALGO_SIPHASH = 2,
};

#define HIFS_BOOT_OFFSET		 0
#define HIFS_BOOT_LEN            512
#define HIFS_BOOT2_OFFSET		 (HIFS_BOOT_LEN + 512)
#define HIFS_BOOT2_LEN           512
#define HIFS_SUPER_OFFSET		 1024
#define HIFS_SUPER2_OFFSET		 (HIFS_SUPER_OFFSET + 1024)
#define HIFS_SUPER2_LEN          1024
#define HIFS_INODE_BITMAP_OFFSET HIFS_DEFAULT_BLOCK_SIZE
#define HIFS_INODE_TABLE_OFFSET	 (HIFS_DEFAULT_BLOCK_SIZE * 2)
#define HIFS_INODE_TABLE2_OFFSET (HIFS_DEFAULT_BLOCK_SIZE * 3)
#define HIFS_DIRTY_TABLE_OFFSET (HIFS_DEFAULT_BLOCK_SIZE * 4)
#define HIFS_ROOT_DENTRY_OFFSET	 (HIFS_DIRTY_TABLE_OFFSET + HIFS_DEFAULT_BLOCK_SIZE)
/* Additional cache bitmaps placed after root direntry block */
#define HIFS_DIRENT_BITMAP_OFFSET (HIFS_ROOT_DENTRY_OFFSET + HIFS_DEFAULT_BLOCK_SIZE)
#define HIFS_BLOCK_BITMAP_OFFSET  (HIFS_DIRENT_BITMAP_OFFSET + HIFS_DEFAULT_BLOCK_SIZE)
/* Default place where FS will start using after mkcache (all above are used for mkcache tables) */
#define HIFS_CACHE_SPACE_START	 (HIFS_BLOCK_BITMAP_OFFSET + HIFS_DEFAULT_BLOCK_SIZE)

#ifdef __KERNEL__
struct hifs_block_fingerprint {
	uint32_t	block_no;   /* logical block number within the file */
	uint8_t		hash[HIFS_BLOCK_HASH_SIZE];
	uint8_t		hash_algo;  /* HIFS_HASH_ALGO_* */
	uint8_t		reserved[3];
};
#else
struct hifs_block_fingerprint {
	uint32_t	block_no;
	uint8_t		hash[HIFS_BLOCK_HASH_SIZE];
	uint8_t		hash_algo;
	uint8_t		reserved[3];
};
#endif

struct hifs_block_fingerprint_wire {
#ifdef __KERNEL__
	__le32	block_no;
	__u8	hash[HIFS_BLOCK_HASH_SIZE];
	__u8	hash_algo;
	__u8	reserved[3];
#else
	uint32_t block_no;
	uint8_t  hash[HIFS_BLOCK_HASH_SIZE];
	uint8_t  hash_algo;
	uint8_t  reserved[3];
#endif
};

#define HIFS_INODE_SIZE		(sizeof(struct hifs_inode))
#define HIFS_INODE_NUMBER_TABLE	128
#define HIFS_INODE_TABLE_SIZE	(HIFS_INODE_NUMBER_TABLE * HIFS_INODE_SIZE)/HIFS_DEFAULT_BLOCK_SIZE

/**
 * Special inode numbers inhereted from Ext2 or new to HIFS...
 * NOTE: Our virtual FS cache uses a basic EXT2 style layout for the most part.
 *       This is a good way to keep things consistent.
 **/
#define HIFS_BAD_INO	     1  /* Bad blocks inode */
#define HIFS_LAF_INO		 2  /* Lost and Found inode nr */
#define HIFS_USR_QUOTA_INO	 3	/* User quota inode */
#define HIFS_GRP_QUOTA_INO	 4	/* Group quota inode */
#define HIFS_BOOT_LOADER_INO 5	/* Boot loader inode */
#define HIFS_UNDEL_DIR_INO	 6	/* Undelete directory inode */
#define HIFS_RESIZE_INO		 7	/* Reserved group descriptors inode */
#define HIFS_JOURNAL_INO	 8	/* Journal inode */
#define HIFS_EXCLUDE_INO	 9	/* The "exclude" inode, for snapshots */
#define HIFS_ROOT_INODE      11 /* Root inode nbr */
#define HIFS_SUPER_POSITION 1024 
#define HIFS_MAX_WRITE_DESCS 256
#define HIFS_INODE_SYNC_BYTES		(512 * 1024)
#define HIFS_INODE_SYNC_INTERVAL	(5 * HZ)
#define HIFS_WRITEBATCH_MAX 256   /* tune: 128 * 4k = 512k at a time */

struct hifs_extent {
	uint32_t block_start;   /* first block (inclusive) */
	uint32_t block_count;   /* number of contiguous blocks in this extent */
};

struct hifs_extent_wire {
#ifdef __KERNEL__
	__le32 block_start;
	__le32 block_count;
#else
	uint32_t block_start;
	uint32_t block_count;
#endif
};

/**
 * The on-Disk inode
 **/
struct hifs_inode
{
	//const struct super_block	i_sb;      /* Superblock position */
    uint8_t     i_version;	/* inode version */
	uint8_t		i_flags;	/* inode flags: TYPE */
	uint32_t	i_mode;		/* File mode */
	uint64_t	i_ino;		/* inode number */
	uint16_t	i_uid;		/* owner's user id */
	uint16_t	i_gid;		/* owner's group id */
	uint16_t	i_hrd_lnk;	/* number of hard links */
	uint32_t    i_atime; /* Archive Time */
	uint32_t	i_mtime; /* Modified Time */
	uint32_t	i_ctime; /* Creation Time */
	uint32_t	i_size;		/* Number of bytes in file */
	char    	i_name[HIFS_MAX_NAME_SIZE]; /* File name */
	//void		*i_private;  /* Private/Unpublished filesystrem member */
	//const struct inode_operations	i_op;       /* operation */
	//const struct file_operations	i_fop;	      /* file operation */
	struct hifs_extent extents[HIFS_INODE_TSIZE];
	uint32_t	i_blocks;	/* Number of blocks */
	uint32_t	i_bytes;	/* Number of bytes */
	uint8_t		i_links;    /* Number of links to an inode */
	struct hifs_block_fingerprint i_block_fingerprints[HIFS_MAX_BLOCK_HASHES];
	uint16_t	i_hash_count;
	uint16_t	i_hash_reserved;
#ifdef __KERNEL__
	size_t		i_runtime_dirty_bytes;
	unsigned long	i_runtime_last_sync;
#endif
};


struct hifs_dir_entry 
{
	uint32_t inode_nr;		/* inode number */
	uint32_t name_len;		/* Name length */
	char name[256];			/* File name, up to HIFS_NAME_LEN */
};

struct hifs_cache_bitmap {
	uint8_t *bitmap;
	uint64_t size;
	uint64_t entries;
	uint64_t cache_block_size;
	uint64_t cache_block_count;
	uint64_t cache_blocks_free;
	uint8_t dirty;
};

/* Cache-level superblock (local cache metadata). This is NOT sent
 * to the cluster; it describes the cache layout on local storage. */
struct hifs_disk_superblock {
#ifdef __KERNEL__
	__le32	s_generational_epoch;   /* epoch counter to control who made the last change in the sb, tie goes to latest s_wtime */
	__le32	s_inodes_count;         /* total inodes */
	__le32	s_blocks_count;         /* total blocks */
	__le32	s_r_blocks_count;       /* reserved blocks */
	__le32	s_free_blocks_count;    /* free blocks */
	__le32	s_free_inodes_count;    /* free inodes */
	__le32	s_first_data_block;     /* first data block */
	__le32	s_log_block_size;       /* block size = 1024<<s_log_block_size */
	__le32	s_log_frag_size;        /* s_log_frag_size (obsolete in ext3+) */
	__le32	s_blocks_per_group;
	__le32	s_frags_per_group;
	__le32	s_inodes_per_group;
	__le32	s_mtime;                /* last mount (UNIX epoch) */
	__le32	s_wtime;                /* last write */
	__le16	s_mnt_count;            /* mounts since fsck */
	__le16	s_max_mnt_count;        /* max mounts before fsck */
	__le16	s_magic;                /* must equal EXT2_SUPER_MAGIC (0xEF53) */
	__le16	s_state;                /* clean/dirty flags */
	__le16	s_errors;               /* behavior on errors */
	__le16	s_minor_rev_level;      /* minor revision */
	__le32	s_lastcheck;            /* last fsck time */
	__le32	s_checkinterval;        /* max interval between fscks */
	__le32	s_creator_os;           /* creator OS */
	__le32	s_rev_level;            /* major revision */
	__le16	s_def_resuid;           /* default reserved UID */
	__le16	s_def_resgid;           /* default reserved GID */

	/* EXT2_DYNAMIC_REV (>=1) additions */
	__le32	s_first_ino;            /* first non-reserved inode */
	__le16	s_inode_size;           /* size of each inode structure */
	__le16	s_block_group_nr;       /* block group this superblock is in */
	__le32  s_feature_compat;       /* compatible feature flags */
	__le32  s_feature_incompat;     /* incompatible feature flags */
	__le32  s_feature_ro_compat;    /* read-only-compatible features */
	__u8	s_uuid[16];             /* filesystem UUID */
	char	s_volume_name[16];      /* volume label (not null-terminated) */
	char	s_last_mounted[64];     /* last mount path */
	__le32	s_algorithm_usage_bitmap; /* for compression (unused) */

	/* Performance hints */
	__u8	s_preallocblocks_;      /* # of blocks to preallocate */
	__u8	s_prealloc_dir_blocks;  /* # to preallocate for dirs */
	__u16	s_reserved_gdt_blocks;  /* number of reserved GDT blocks */

	/* Directory hashing */
	__le32	s_hash_seed[4];
	__u8	s_def_hash_version;
	__u8	s_reserved_char_pad;
	__le16	s_reserved_word_pad;

	/* Journaling backup */
	__le32	s_default_mount_opts;
	__le32	s_first_meta_bg;
	__le32	s_mkfs_time;
	__le32	s_jnl_blocks[17];

	/* EXT4 (64-bit) additions follow */
	__le32	s_blocks_count_hi;
	__le32	s_r_blocks_count_hi;
	__le32	s_free_blocks_hi;
	__le16	s_min_extra_isize;
	__le16	s_want_extra_isize;
	__le32	s_flags;
	__le16	s_raid_stride;
	__le16	s_mmp_interval;
	__le64	s_mmp_block;
	__le32	s_raid_stripe_width;
	__u8	s_log_groups_per_flex;
	__u8	s_checksum_type;
	__u8	s_encryption_level;
	__u8	s_reserved_pad;
	__le64	s_kbytes_written;
	__le32	s_snapshot_inum;
	__le32	s_snapshot_id;
	__le64	s_snapshot_r_blocks_count;
	__le32	s_snapshot_list;
	__le32	s_error_count;
	__le32	s_first_error_time;
	__le32	s_first_error_ino;
	__le64	s_first_error_block;
	char	s_first_error_func[32];
	__le32	s_first_error_line;
	__le32	s_last_error_time;
	__le32	s_last_error_ino;
	__le64	s_last_error_block;
	char	s_last_error_func[32];
	__le32	s_last_error_line;
	__le32	s_mount_opts;
	__le32	s_usr_quota_inum;
	__le32	s_grp_quota_inum;
	__le32	s_overhead_blocks;
	__le32	s_backup_bgs[2];
	__le32	s_encrypt_algos[4];
	__le32	s_encrypt_pw_salt[4];
	__le32	s_lpf_ino;
	__le32	s_prj_quota_inum;
	__le32  s_checksum_seed;
	__u8	s_wtime_hi;
	__u8	s_mtime_hi;
	__u8	s_mkfs_time_hi;
	__u8	s_lastcheck_hi;
	__u8	s_first_error_time_hi;
	__u8	s_last_error_time_hi;
	__u8	s_padding1[2];
	__le32	s_reserved[96];         /* must fill superblock to 1024 bytes */
	__le32	s_checksum;             /* superblock checksum */
#else
	uint32_t	s_generational_epoch;
	uint32_t	s_inodes_count;
	uint32_t	s_blocks_count;
	uint32_t	s_r_blocks_count;
	uint32_t	s_free_blocks_count;
	uint32_t	s_free_inodes_count;
	uint32_t	s_first_data_block;
	uint32_t	s_log_block_size;
	uint32_t	s_log_frag_size;
	uint32_t	s_blocks_per_group;
	uint32_t	s_frags_per_group;
	uint32_t	s_inodes_per_group;
	uint32_t	s_mtime;
	uint32_t	s_wtime;
	uint16_t	s_mnt_count;
	uint16_t	s_max_mnt_count;
	uint16_t	s_magic;
	uint16_t	s_state;
	uint16_t	s_errors;
	uint16_t	s_minor_rev_level;
	uint32_t	s_lastcheck;
	uint32_t	s_checkinterval;
	uint32_t	s_creator_os;
	uint32_t	s_rev_level;
	uint16_t	s_def_resuid;
	uint16_t	s_def_resgid;
	/* EXT2_DYNAMIC_REV (>=1) additions */
	uint32_t	s_first_ino;
	uint16_t	s_inode_size;
	uint16_t	s_block_group_nr;
	uint32_t  s_feature_compat;
	uint32_t  s_feature_incompat;
	uint32_t  s_feature_ro_compat;
	uint8_t	s_uuid[16];
	char	s_volume_name[16];
	char	s_last_mounted[64];
	uint32_t	s_algorithm_usage_bitmap;
	/* Performance hints */
	uint8_t	s_prealloc_blocks;
	uint8_t	s_prealloc_dir_blocks;
	uint16_t	s_reserved_gdt_blocks;
	/* Directory hashing */
	uint32_t	s_hash_seed[4];
	uint8_t	s_def_hash_version;
	uint8_t	s_reserved_char_pad;
	uint16_t	s_reserved_word_pad;
	/* Journaling backup */
	uint32_t	s_default_mount_opts;
	uint32_t	s_first_meta_bg;
	uint32_t	s_mkfs_time;
	uint32_t	s_jnl_blocks[17];
	/* EXT4 (64-bit) additions follow */
	uint32_t	s_blocks_count_hi;
	uint32_t	s_r_blocks_count_hi;
	uint32_t	s_free_blocks_hi;
	uint16_t	s_min_extra_isize;
	uint16_t	s_want_extra_isize;
	uint32_t	s_flags;
	uint16_t	s_raid_stride;
	uint16_t	s_mmp_interval;
	uint64_t	s_mmp_block;
	uint32_t	s_raid_stripe_width;
	uint8_t	s_log_groups_per_flex;
	uint8_t	s_checksum_type;
	uint8_t	s_encryption_level;
	uint8_t	s_reserved_pad;
	uint64_t	s_kbytes_written;
	uint32_t	s_snapshot_inum;
	uint32_t	s_snapshot_id;
	uint64_t	s_snapshot_r_blocks_count;
	uint32_t	s_snapshot_list;
	uint32_t	s_error_count;
	uint32_t	s_first_error_time;
	uint32_t	s_first_error_ino;
	uint64_t	s_first_error_block;
	char	s_first_error_func[32];
	uint32_t	s_first_error_line;
	uint32_t	s_last_error_time;
	uint32_t	s_last_error_ino;
	uint64_t	s_last_error_block;
	char	s_last_error_func[32];
	uint32_t	s_last_error_line;
	uint32_t	s_mount_opts;
	uint32_t	s_usr_quota_inum;
	uint32_t	s_grp_quota_inum;
	uint32_t	s_overhead_blocks;
	uint32_t	s_backup_bgs[2];
	uint32_t	s_encrypt_algos[4];
	uint32_t	s_encrypt_pw_salt[4];
	uint32_t	s_lpf_ino;
	uint32_t	s_prj_quota_inum;
	uint32_t  s_checksum_seed;
	uint8_t	s_wtime_hi;
	uint8_t	s_mtime_hi;
	uint8_t	s_mkfs_time_hi;
	uint8_t	s_lastcheck_hi;
	uint8_t	s_first_error_time_hi;
	uint8_t	s_last_error_time_hi;
	uint8_t	s_padding1[2];
	uint32_t	s_reserved[96];
	uint32_t	s_checksum;
#endif
};

/* Remote-facing per-volume logical superblock (minimal fields used for
 * reconciliation with the cluster). This is exchanged over the comm link.
 * Do not confuse with the cache super above. */
struct hifs_volume_superblock {
#ifdef __KERNEL__
    __le32 s_magic;      /* identifies filesystem type */
    __le32 s_blocksize;  /* fundamental block size */
    __le32 s_blocksize_bits; /* log2(blocksize) */
    __le64 s_blocks_count;   /* total blocks in volume */
    __le64 s_free_blocks;    /* free blocks available */
    __le64 s_inodes_count;   /* total inodes */
    __le64 s_free_inodes;    /* free inodes */
    __le64 s_maxbytes;       /* max file size supported */
    __le32 s_feature_compat;     /* compatible feature flags */
    __le32 s_feature_ro_compat;  /* read-only compatible features */
    __le32 s_feature_incompat;   /* incompatible features */
    __u8   s_uuid[16];           /* volume UUID */
    __le32 s_rev_level;   /* epoch/generation */
    __le32 s_wtime;       /* last write time (epoch) */
    __le32 s_flags;       /* optional flags */
#else
    uint32_t s_magic;
    uint32_t s_blocksize;
    uint32_t s_blocksize_bits;
    uint64_t s_blocks_count;
    uint64_t s_free_blocks;
    uint64_t s_inodes_count;
    uint64_t s_free_inodes;
    uint64_t s_maxbytes;
    uint32_t s_feature_compat;
    uint32_t s_feature_ro_compat;
    uint32_t s_feature_incompat;
    uint8_t  s_uuid[16];
    uint32_t s_rev_level;
    uint32_t s_wtime;
    uint32_t s_flags;
#endif
    char     s_volume_name[16]; /* optional label */
};

/* Remote-facing root dentry metadata for reconciliation with the cluster. */
struct hifs_volume_root_dentry {
#ifdef __KERNEL__
    __le64 rd_inode;       /* inode number for root */
    __le32 rd_mode;        /* permission bits */
    __le32 rd_uid;         /* owner uid */
    __le32 rd_gid;         /* owner gid */
    __le32 rd_flags;       /* inode flags */
    __le64 rd_size;        /* directory size */
    __le64 rd_blocks;      /* blocks allocated */
    __le32 rd_atime;       /* last access time */
    __le32 rd_mtime;       /* last modification */
    __le32 rd_ctime;       /* last status change */
    __le32 rd_links;       /* hard link count */
    __le32 rd_name_len;    /* length of rd_name */
#else
    uint64_t rd_inode;
    uint32_t rd_mode;
    uint32_t rd_uid;
    uint32_t rd_gid;
    uint32_t rd_flags;
    uint64_t rd_size;
    uint64_t rd_blocks;
    uint32_t rd_atime;
    uint32_t rd_mtime;
    uint32_t rd_ctime;
    uint32_t rd_links;
    uint32_t rd_name_len;
#endif
    char     rd_name[HIFS_MAX_NAME_SIZE]; /* root entry label */
};

/* SB message wrapper carried over data FIFO for SB_SEND/SB_RECV. */
struct hifs_sb_msg {
#ifdef __KERNEL__
    __u64 volume_id;              /* identifies the mount/volume */
#else
    uint64_t volume_id;
#endif
    struct hifs_volume_superblock vsb;   /* logical super */
};

/* Root dentry metadata exchange wrapper. */
struct hifs_root_msg {
#ifdef __KERNEL__
    __u64 volume_id;
#else
    uint64_t volume_id;
#endif
    struct hifs_volume_root_dentry root;
};

/* Generic directory entry metadata shared with the cluster. */
struct hifs_volume_dentry {
#ifdef __KERNEL__
    __le32 de_flags;
    __le64 de_parent;     /* parent inode */
    __le64 de_inode;      /* child inode */
    __le32 de_epoch;      /* update timestamp (ms) */
    __le32 de_type;       /* DT_* style file type */
    __le32 de_name_len;   /* name length */
#else
    uint32_t de_flags;
    uint64_t de_parent;
    uint64_t de_inode;
    uint32_t de_epoch;
    uint32_t de_type;
    uint32_t de_name_len;
#endif
    char     de_name[HIFS_MAX_NAME_SIZE];
};

struct hifs_dentry_msg {
#ifdef __KERNEL__
    __u64 volume_id;
#else
    uint64_t volume_id;
#endif
    struct hifs_volume_dentry dentry;
};

struct hifs_block_msg {
#ifdef __KERNEL__
    __u64 volume_id;
    __le64 block_no;
    __le32 flags;
    __le32 data_len;
    __u8  hash_algo;
    __u8  hash_reserved[3];
    __u8  hash[HIFS_BLOCK_HASH_SIZE];
#else
    uint64_t volume_id;
    uint64_t block_no;
    uint32_t flags;
    uint32_t data_len;
    uint8_t  hash_algo;
    uint8_t  hash_reserved[3];
    uint8_t  hash[HIFS_BLOCK_HASH_SIZE];
#endif
};

struct hifs_contig_block_stream {
#ifdef __KERNEL__
	__le64 block_start;
	__le32 block_count;
	__le32 reserved;
#else
	uint64_t block_start;
	uint32_t block_count;
	uint32_t reserved;
#endif
	/* Followed by block_count little-endian uint32 lengths then raw bytes */
};

struct hifs_block_hash_wire {
#ifdef __KERNEL__
	__u8  hash_algo;
	__u8  reserved[3];
	__u8  hash[HIFS_BLOCK_HASH_SIZE];
#else
	uint8_t  hash_algo;
	uint8_t  reserved[3];
	uint8_t  hash[HIFS_BLOCK_HASH_SIZE];
#endif
};

/* Wire format for hifs_inode exchanged with cluster. */
struct hifs_inode_wire {
#ifdef __KERNEL__
    __le32 i_msg_flags;
    __u8  i_version;
    __u8  i_flags;
    __le32 i_mode;
    __le64 i_ino;
    __le16 i_uid;
    __le16 i_gid;
    __le16 i_hrd_lnk;
    __le32 i_atime;
    __le32 i_mtime;
    __le32 i_ctime;
    __le32 i_size;
    __u8  i_name[HIFS_MAX_NAME_SIZE];
    struct hifs_extent_wire extents[HIFS_INODE_TSIZE];
    __le32 i_blocks;
    __le32 i_bytes;
    __u8  i_links;
    __u8  __pad[3];
    struct hifs_block_fingerprint_wire i_block_fingerprints[HIFS_MAX_BLOCK_HASHES];
    __le16 i_hash_count;
    __le16 i_hash_reserved;
	__le16 i_hash_head;
#else
    uint32_t i_msg_flags;
    uint8_t  i_version;
    uint8_t  i_flags;
    uint32_t i_mode;
    uint64_t i_ino;
    uint16_t i_uid;
    uint16_t i_gid;
    uint16_t i_hrd_lnk;
    uint32_t i_atime;
    uint32_t i_mtime;
    uint32_t i_ctime;
    uint32_t i_size;
    uint8_t  i_name[HIFS_MAX_NAME_SIZE];
    struct hifs_extent_wire extents[HIFS_INODE_TSIZE];
    uint32_t i_blocks;
    uint32_t i_bytes;
    uint8_t  i_links;
    uint8_t  __pad[3];
    struct hifs_block_fingerprint_wire i_block_fingerprints[HIFS_MAX_BLOCK_HASHES];
    uint16_t i_hash_count;
    uint16_t i_hash_reserved;
	uint16_t i_hash_head;
#endif
};

struct hifs_inode_msg {
#ifdef __KERNEL__
    __u64 volume_id;
#else
    uint64_t volume_id;
#endif
    struct hifs_inode_wire inode;
};

/* Volume table lives after the block bitmap by default. Each entry maps a
 * volume_id to its logical superblock used for reconciliation between the
 * cluster and with the local kernel VFS layer.
 * */
#define HIFS_VOLUME_TABLE_OFFSET   (HIFS_BLOCK_BITMAP_OFFSET + HIFS_DEFAULT_BLOCK_SIZE)
#define HIFS_VOLUME_TABLE_MAX      1024
#define HIFS_VOLUME_CACHE_ID       0ULL  /* volume_id that refers to the on-disk cache image */

struct hifs_volume_entry {
#ifdef __KERNEL__
    __le64 volume_id;            /* 0 means free */
#else
    uint64_t volume_id;
#endif
    struct hifs_volume_superblock vsb;  /* minimal remote-facing super */
    struct hifs_volume_root_dentry root; /* root dentry metadata */
};

/***********************
 * END Hive FS Structures
 ***********************/
#define HIFS_INODE_MSGF_REQUEST   0x00000001
#define HIFS_BLOCK_MSGF_REQUEST   0x00000001
#define HIFS_BLOCK_MSGF_CONTIG_START 0x00000002
#define HIFS_BLOCK_MSGF_CONTIG_END   0x00000004
