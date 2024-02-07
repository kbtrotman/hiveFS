/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#ifndef _LINUX_HIVEFS_DEFS_H
#define _LINUX_HIVEFS_DEFS_H

/***********************
 * Netlink Structures
 ***********************/
#define HIFS_GENL_NAME "hifs_genl"
#define HIFS_GENL_VERSION 1
#define HIFS_MC_GRP_NAME "mcgrp"
#define MAX_PAYLOAD 4296

// Define the payload
extern enum 
{
	HIFS_GENL_ATB_IVERS,
	HIFS_GENL_ATB_IFLAGS,
	HIFS_GENL_ATB_IMODE,
	HIFS_GENL_ATB_I_ID,
	HIFS_GENL_ATB_IUID,
	HIFS_GENL_ATB_IGID,
	HIFS_GENL_ATB_IHRD_LNK,
	HIFS_GENL_ATB_ICTIME,
	HIFS_GENL_ATB_IMTIME,
	HIFS_GENL_ATB_ISIZE,
	HIFS_GENL_ATB_INAME,
	HIFS_GENL_ATB_IADDRB,
	HIFS_GENL_ATB_IADDRE,
    __HIFS_GENL_ATB_MAX,
} hive_payload_attrs;

#define HIFS_GENL_ATB_MAX (__HIFS_GENL_ATB_MAX - 1)

// Define the Netlink commands
extern enum 
{
	HIFS_GENL_CDM_SEND_ACK,			    // Hive Send Ack to/from User Space
    HIFS_GENL_CDM_SET_LINK_UP,          // Hive Operational to/from User Space
    HIFS_GENL_CDM_SET_LINK_DOWN,        // Hive Shutdown to/from User Space
    HIFS_GENL_CDM_SET_LINK_PULSE,       // Periodic I'm Alive Pulse, when needed
    HIFS_GENL_CDM_SET_LINK_RESET,       // Hive Reset to/from User Space
    HIFS_GENL_CDM_SYNC_SUPERBLOCK,      // Hive Sync Superblock to/from User Space
    HIFS_GENL_CDM_SEND_INODE_ONLY,      // Hive Send Inode Only to/from User Space
    HIFS_GENL_CDM_SEND_INODE_AND_BLOCK, // Hive Send Inode and Block to/from User Space
    HIFS_GENL_CDM_SEND_INODE_AND_FILE,  // Hive Send Inode and File to/from User Space
    HIFS_GENL_CDM_SEND_BLOCK_ONLY,      // Hive Send Block Only to/from User Space
    HIFS_GENL_CDM_SEND_FILE_ONLY,       // Hive Send File Only to/from User Space
    HIFS_GENL_CDM_REQ_INODE_ONLY,       // User Space Request Inode Only to Hive
    HIFS_GENL_CDM_REQ_INODE_AND_BLOCK,  // User Space Request Inode and Block to Hive
    HIFS_GENL_CDM_REQ_INODE_AND_FILE,   // User Space Req Inode and File to/from Hive
    HIFS_GENL_CDM_REQ_BLOCK_ONLY,       // User Space Request Block Only to/from Hive
    HIFS_GENL_CDM_REQ_FILE_ONLY,        // User Space Request File Only to/from Hive
	__HIFS_GENL_CDM_MAX,
} hive_commands;

#define HIFS_GENL_CDM_MAX (__HIFS_GENL_CDM_MAX - 1)


/***********************
 * HiveFS Structures
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
#define HIFS_DEFAULT_BSIZE	4096
#define HIFS_OLT_OFFSET		(HIFS_SUPER_OFFSET + 1)
#define HIFS_INODE_TABLE_OFFSET	(HIFS_OLT_OFFSET + 1)
#define HIFS_INODE_BITMAP_OFFSET	(HIFS_INODE_TABLE_OFFSET + HIFS_INODE_TABLE_SIZE + 1)
#define HIFS_ROOT_INODE_OFFSET	(HIFS_INODE_BITMAP_OFFSET + 1)
#define HIFS_ROOT_IN_EXT_OFF	(HIFS_ROOT_INODE_OFFSET + 1)
#define HIFS_LF_INODE_OFFSET	(HIFS_ROOT_IN_EXT_OFF + HIFS_DEF_ALLOC)

/* Default place where FS will start using after mkfs (all above are used for mkfs) */
#define DM_FS_SPACE_START	(HIFS_LF_INODE_OFFSET + HIFS_DEF_ALLOC)

/* FS constants */
#define HIFS_MAGIC_NR		0xf00dbeef
#define HIFS_INODE_SIZE		512
#define HIFS_INODE_NUMBER_TABLE	128
#define HIFS_INODE_TABLE_SIZE	(HIFS_INODE_NUMBER_TABLE * HIFS_INODE_SIZE)/HIFS_DEFAULT_BSIZE
#define HIFS_EMPTY_ENTRY		0xdeeddeed

#define HIFS_NAME_LEN		255
#define HIFS_DEF_ALLOC		4	/* By default alloc N blocks per extend */

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
};

struct hifs_dir_entry 
{
	uint32_t inode_nr;		/* inode number */
	uint32_t name_len;		/* Name length */
	char name[256];			/* File name, up to HIFS_NAME_LEN */
};

#endif /* _LINUX_HIVEFS_DEFS_H */

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