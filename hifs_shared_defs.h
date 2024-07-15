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
#define GET_TIME() (jiffies * 1000 / HZ)
#include <linux/list.h>
#else
#include <time.h>
#define GET_TIME() (clock() * 1000 / CLOCKS_PER_SEC)
#include "hicomm/hi_user_double_linked_list.h"
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
#define HIFS_QUEUE_COUNT 3

#define DEVICE_FILE_INODE "hivefs_comq_inode"     //Device queue names
#define DEVICE_FILE_BLOCK "hivefs_comq_block"
#define DEVICE_FILE_CMDS "hivefs_comq_cmds"
#define ATOMIC_KERN_DEVICE_NAME "hivefs_kern_link_device"   //Atomic sync device. Used to ensure we have a link to send data.
#define ATOMIC_KERN_CLASS_NAME "hivefs_kern_atomic_class"
#define ATOMIC_USER_DEVICE_NAME "hivefs_user_link_device"   //Atomic sync device. Used to ensure we have a link to send data.
#define ATOMIC_USER_CLASS_NAME "hivefs_user_atomic_class"

#define HIFS_DEFAULT_BLOCK_SIZE 4096
#define HIFS_BUFFER_SIZE 4096
#define HIFS_MAX_CMD_SIZE 50 // MAX_CMD_SIZE is the maximum size of a command
#define HIFS_MAX_NAME_SIZE 256 //MAX NAME SIZE is the maximum size of a file name, dir name, or other name in FS.

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

enum hifs_module{HIFS_COMM_PROGRAM_KERN_MOD, HIFS_COMM_PROGRAM_USER_HICOMM};
enum hifs_queue_direction{HIFS_COMM_TO_USER, HIFS_COMM_FROM_USER};

extern struct pollfd *cmd_pfd;
extern struct pollfd *inode_pfd;
extern struct pollfd *block_pfd;

struct hifs_blocks {
	int block_size;
	int count;
	char *block;
	struct list_head hifs_block_list;
};

struct hifs_blocks_user {
	int block_size;
	int count;
	char block[HIFS_DEFAULT_BLOCK_SIZE];
};

struct hifs_cmds {
	int count;
	char *cmd;
	struct list_head hifs_cmd_list;
};

struct hifs_cmds_user {
    int count;
	char cmd[HIFS_MAX_CMD_SIZE];
};

extern int k_major, u_major, i_major, b_major, c_major;  

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

#define free_a_list(lia, type, member) \
do { \
    type *node, *tmp; \
    list_for_each_entry_safe(node, tmp, lia, member) { \
        list_del(&node->member); \
        if (node) { kfree(node); } \
    } \
} while (0)
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
#define HIFS_ROOT_INODE_OFFSET	 (HIFS_INODE_TABLE2_OFFSET + HIFS_DEFAULT_BLOCK_SIZE)
/* Default place where FS will start using after mkfs (all above are used for mkfs) */
#define HIFS_CACHE_SPACE_START	 (HIFS_INODE_TABLE2_OFFSET + HIFS_DEFAULT_BLOCK_SIZE)

#define HIFS_INODE_SIZE		512
#define HIFS_INODE_NUMBER_TABLE	128
#define HIFS_INODE_TABLE_SIZE	(HIFS_INODE_NUMBER_TABLE * HIFS_INODE_SIZE)/HIFS_DEFAULT_BLOCK_SIZE

/**
 * Special inode numbers inhereted from Ext2 or new to HIFS...
 * NOTE: Our virtual FS cache uses a basic EXT2 style layout for the most part.
 *       This is a good way to keep things consistent.
 **/
#define EXT2_BAD_INO	     1  /* Bad blocks inode */
#define EXT2_LAF_INO		 2  /* Lost and Found inode nr */
#define EXT2_USR_QUOTA_INO	 3	/* User quota inode */
#define EXT2_GRP_QUOTA_INO	 4	/* Group quota inode */
#define EXT2_BOOT_LOADER_INO 5	/* Boot loader inode */
#define EXT2_UNDEL_DIR_INO	 6	/* Undelete directory inode */
#define EXT2_RESIZE_INO		 7	/* Reserved group descriptors inode */
#define EXT2_JOURNAL_INO	 8	/* Journal inode */
#define EXT2_EXCLUDE_INO	 9	/* The "exclude" inode, for snapshots */
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
	uint32_t    i_atime; /* Archive Time */
	uint32_t	i_mtime; /* Modified Time */
	uint32_t	i_ctime; /* Creation Time */
	uint32_t	i_size;		/* Number of bytes in file */
	char    	i_name[HIFS_MAX_NAME_SIZE]; /* File name */
	void		*i_private;  /* Private/Unpublished filesystrem member */
	const struct inode_operations	*i_op;       /* operation */
	const struct file_operations	*i_fop;	      /* file operation */
	/* address begin - end block, range exclusive: addres end (last block) does not belogs to extend! */
	uint32_t	i_addrb[HIFS_INODE_TSIZE];	/* Start block of extend ranges */
	uint32_t	i_addre[HIFS_INODE_TSIZE];	/* End block of extend ranges */
	uint32_t	i_blocks;	/* Number of blocks */
	uint32_t	i_bytes;	/* Number of bytes */
	struct list_head hifs_inode_list;
};

struct hifs_inode_user 
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
};


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



/***********************
 * END Hive FS Structures
 ***********************/
