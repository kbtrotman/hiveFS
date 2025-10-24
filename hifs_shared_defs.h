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
#define GET_TIME() (clock() * 1000 / CLOCKS_PER_SEC)
#endif


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
#endif // __KERNEL__

/******************************
 * Base FS Information
 ******************************/
/* FS Name */
#define HIFS_NAME		"hivefs"
#define HIFS_VERSION	"0.1"
/* FS Layout version/schema */
#define HIFS_LAYOUT_VER		1

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

#define HIFS_COMM_DEVICE_NAME "hivefs_ctl"
#define HIFS_CMD_RING_CAPACITY 128
#define HIFS_INODE_RING_CAPACITY 128

#define HIFS_IOCTL_MAGIC      'H'
#define HIFS_IOCTL_CMD_DEQUEUE    _IOWR(HIFS_IOCTL_MAGIC, 0, struct hifs_cmds)
#define HIFS_IOCTL_INODE_DEQUEUE  _IOWR(HIFS_IOCTL_MAGIC, 1, struct hifs_inode)
#define HIFS_IOCTL_STATUS         _IOR(HIFS_IOCTL_MAGIC,  2, struct hifs_comm_status)
#define HIFS_IOCTL_CMD_ENQUEUE    _IOW (HIFS_IOCTL_MAGIC, 3, struct hifs_cmds)
#define HIFS_IOCTL_INODE_ENQUEUE  _IOW (HIFS_IOCTL_MAGIC, 4, struct hifs_inode)

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
#define HIFS_Q_PROTO_CMD_FLUSH       "cache_flush_all"
#define HIFS_Q_PROTO_CMD_BLOCK_RECV  "block_recv"
#define HIFS_Q_PROTO_CMD_BLOCK_SEND  "block_send"
#define HIFS_Q_PROTO_CMD_INODE_RECV  "inode_recv"
#define HIFS_Q_PROTO_CMD_INODE_SEND  "inode_send"
#define HIFS_Q_PROTO_CMD_FILE_RECV   "file_recv"
#define HIFS_Q_PROTO_CMD_FILE_SEND   "file_send"
#define HIFS_Q_PROTO_CMD_ENGINE_VERS "engine_version"
#define HIFS_Q_PROTO_CMD_TEST        "test_cmd"
#define HIFS_Q_PROTO_CMD_LINK_UP     "link_up"
#define HIFS_Q_PROTO_CMD_LINK_INIT   "link_init"
#define HIFS_Q_PROTO_CMD_LINK_READY  "link_ready"
#define HIFS_Q_PROTO_CMD_LINK_DOWN   "link_down"

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
/* Default place where FS will start using after mkfs (all above are used for mkfs) */
#define HIFS_CACHE_SPACE_START	 (HIFS_INODE_TABLE2_OFFSET + (HIFS_DEFAULT_BLOCK_SIZE * 5))

#define HIFS_INODE_SIZE		128
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
	/* address begin - end block, range exclusive: addres end (last block) does not belogs to extend! */
	uint32_t	i_addrb[HIFS_INODE_TSIZE];	/* Start block of extend ranges */
	uint32_t	i_addre[HIFS_INODE_TSIZE];	/* End block of extend ranges */
	uint32_t	i_blocks;	/* Number of blocks */
	uint32_t	i_bytes;	/* Number of bytes */
	uint8_t		i_links;    /* Number of links to an inode */
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

struct hifs_disk_superblock {
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
	__u8	s_prealloc_blocks;      /* # of blocks to preallocate */
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
};

/***********************
 * END Hive FS Structures
 ***********************/
