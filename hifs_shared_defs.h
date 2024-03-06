/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#pragma once

/******************************
 * Queue Management Structures
 ******************************/
// These definitions are for 3 shared memory areas (queues).
// The kernel module and hi_command user-space program
// shared these memory areas to send and receive data.
#define HIFS_QUEUE_COUNT 3

#define DEVICE_FILE_INODE "hivefs_comms_inode"
#define DEVICE_FILE_BLOCK "hivefs_comms_block"
#define DEVICE_FILE_CMDS "hivefs_comms_cmds"
#define ATOMIC_DEVICE_NAME "hifs_atomic_sync_device"
#define ATOMIC_CLASS_NAME "atomic_class"

#define HIFS_DEFAULT_BLOCK_SIZE 4096
#define HIFS_BUFFER_SIZE 4096

/******************************
 * Queue Comms Protocol
 ******************************/
// Our protocol for comms using the comms queues.
// these values are set in the atomic variable.
// The atomic variable is a shared memory space
// between kernel and user space. For Hi_Command.
#define HIFS_Q_PROTO_VERSION  1          // Version of the protocol
#define HIFS_Q_PROTO_UNUSED 0            // Queue is not in use
#define HIFS_Q_PROTO_KERNEL_LOCK 1       // Kernel has locked the queue
#define HIFS_Q_PROTO_KERNEL_WO_USER 2    // Kernel is waiting on user
#define HIFS_Q_PROTO_USER_LOCK 3         // User has locked the queue
#define HIFS_Q_PROTO_USER_WO_KERNEL 4    // User is waiting on kernel
// Leave some locking variables here for future use....
#define HIFS_Q_PROTO_ACK_LINK_UP 8       // Acknowledge link up command
#define HIFS_Q_PROTO_ACK_LINK_KERN 9     // Kernel initiated a link up command
#define HIFS_Q_PROTO_ACK_LINK_USER 10    // User initiated a link up command


extern int major;
extern struct hifs_inode *shared_inode_outgoing;    // These six Doubly-Linked Lists are our
extern struct hifs_blocks *shared_block_outgoing;   // processing queues. They are sent & 
extern struct hifs_cmds *shared_cmd_outgoing;       // received thru the 3 device files known
extern struct hifs_inode *shared_inode_incoming;    // as the "queues" (to hi_command). We want
extern struct hifs_blocks *shared_block_incoming;   // to proces them fast, so they're split into
extern struct hifs_cmds *shared_cmd_incoming;       // incoming & outgoing queues here.
// List pointers, for initializing the lists
extern struct list_head shared_inode_outgoing_lst;    
extern struct list_head shared_block_outgoing_lst;    
extern struct list_head shared_cmd_outgoing_lst;       
extern struct list_head shared_inode_incoming_lst;    
extern struct list_head shared_block_incoming_lst;   
extern struct list_head shared_cmd_incoming_lst;       
extern char *filename;     // The filename we're currently sending/recieving to/from.

struct hifs_blocks {
	char *buffer;
	int count;
#ifdef __KERNEL__
	struct list_head hifs_block_list;
#else
	struct hifs_blocks *prev, *next;
#endif
};

struct hifs_cmds {
	char *cmd;
	int count;
#ifdef __KERNEL__
		struct list_head hifs_cmd_list;
#else
		struct hifs_cmds *prev, *next;
#endif
};


#ifdef __KERNEL__
#include <linux/jiffies.h>
#define GET_TIME() (jiffies * 1000 / HZ)
#else
#include <time.h>
#define GET_TIME() (clock() * 1000 / CLOCKS_PER_SEC)
#endif // __KERNEL__

enum hifs_link_state{HIFS_COMM_LINK_DOWN, HIFS_COMM_LINK_UP};
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
	char mount_point[255];
} settings;

/* Layout version */
#define HIFS_LAYOUT_VER		1

/* FS SIZE/OFFSET CONST */
#define HIFS_INODE_TSIZE		3
#define HIFS_SUPER_OFFSET		0
#define HIFS_OLT_OFFSET		(HIFS_SUPER_OFFSET + 1)
#define HIFS_INODE_TABLE_OFFSET	(HIFS_OLT_OFFSET + 1)
#define HIFS_INODE_BITMAP_OFFSET	(HIFS_INODE_TABLE_OFFSET + HIFS_INODE_TABLE_SIZE + 1)
#define HIFS_ROOT_INODE_OFFSET	(HIFS_INODE_BITMAP_OFFSET + 1)
#define HIFS_ROOT_IN_EXT_OFF	(HIFS_ROOT_INODE_OFFSET + 1)
#define HIFS_LF_INODE_OFFSET	(HIFS_ROOT_IN_EXT_OFF + HIFS_DEF_ALLOC)

/* Default place where FS will start using after mkfs (all above are used for mkfs) */
#define HIFS_CACHE_SPACE_START	(HIFS_LF_INODE_OFFSET + HIFS_DEF_ALLOC)

/* FS constants */
#define HIFS_MAGIC_NR		0xf00dbeef
#define HIFS_INODE_SIZE		512
#define HIFS_INODE_NUMBER_TABLE	128
#define HIFS_INODE_TABLE_SIZE	(HIFS_INODE_NUMBER_TABLE * HIFS_INODE_SIZE)/HIFS_DEFAULT_BLOCK_SIZE
#define HIFS_EMPTY_ENTRY		0xdeeddeed

#define HIFS_NAME_LEN		255

/**
 * Special inode numbers 
 **/
#define EXT4_BAD_INO	     1  /* Bad blocks inode */
#define EXT4_LAF_INO		 2  /* Lost and Found inode nr */
#define EXT4_USR_QUOTA_INO	 3	/* User quota inode */
#define EXT4_GRP_QUOTA_INO	 4	/* Group quota inode */
#define EXT4_BOOT_LOADER_INO 5	/* Boot loader inode */
#define EXT4_UNDEL_DIR_INO	 6	/* Undelete directory inode */
#define EXT4_RESIZE_INO		 7	/* Reserved group descriptors inode */
#define EXT4_JOURNAL_INO	 8	/* Journal inode */
#define EXT4_EXCLUDE_INO	 9	/* The "exclude" inode, for snapshots */
#define HIFS_ROOT_INODE      15  /* Root inode nbr */

/**
 * The on-Disk inode
 **/
struct hifs_inode 
{
	struct super_block	*i_sb;      /* Superblock position */
    uint8_t     i_version;	/* inode version */
	uint8_t		i_flags;	/* inode flags: TYPE */
	uint32_t	i_mode;		/* File mode */
	uint64_t	i_ino;		/* inode number */
	uint16_t	i_uid;		/* owner's user id */
	uint16_t	i_gid;		/* owner's group id */
	uint16_t	i_hrd_lnk;	/* number of hard links */
	uint32_t i_atime; /* Archive Time */
	uint32_t	i_mtime; /* Modified Time */
	uint32_t	i_ctime; /* Creation Time */
	uint32_t	i_size;		/* Number of bytes in file */
	char    	i_name[50]; /* File name */
	void		*i_private;  /* Private/Unpublished filesystrem member */
	const struct inode_operations	*i_op;       /* operation */
	const struct file_operations	*i_fop;	      /* file operation */
	/* address begin - end block, range exclusive: addres end (last block) does not belogs to extend! */
	uint32_t	i_addrb[HIFS_INODE_TSIZE];	/* Start block of extend ranges */
	uint32_t	i_addre[HIFS_INODE_TSIZE];	/* End block of extend ranges */
	uint32_t	i_blocks;	/* Number of blocks */
	uint32_t	i_bytes;	/* Number of bytes */
#ifdef __KERNEL__
	struct list_head hifs_inode_list;
#else
	struct hifs_inode *prev, *next;
#endif
};

extern struct list_head hifs_inode_listhead;
struct hifs_dir_entry 
{
	uint32_t inode_nr;		/* inode number */
	uint32_t name_len;		/* Name length */
	char name[256];			/* File name, up to HIFS_NAME_LEN */
};
/***********************
 * END Hive FS Structures
 ***********************/


#ifdef HIVEFS_DEBUG
#define hifs_debug(f, a...)						\
	do 
	{								\
		printk(KERN_DEBUG "hive-fs DEBUG (%s, %d): %s:",	\
			__FILE__, __LINE__, __func__);			\
		printk(KERN_DEBUG f, ## a);				\
	} while (0)
#else
#define hifs_debug(fmt, ...)	no_printk(fmt, ##__VA_ARGS__)
#endif /* _HIVEFS_DEBUG */