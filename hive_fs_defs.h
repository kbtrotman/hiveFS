#ifndef _LINUX_HIVEFS_H
#define _LINUX_HIVEFS_H
/**
#ifdef __KERNEL__
#include <linux/types.h>
#endif
**/

/***********************
 * Database Constants
 ***********************/
#define DATABASE "hiveFS"
#define USER "postgres"
#define PASSWORD "Postgres!909"
#define HIVE "10.100.122.20"
#define PORT "5432"
#define NO_RECORDS 0x099
#define DBSTRING "user=" USER " dbname=" DATABASE " password=" PASSWORD " hostaddr=" HOST " port=" PORT

/* SQL Connect */
struct {
	static PGconn *hive_conn;	/* Connection to hive */
	static PGresult   *last_qury; /* Last query result */
	static PGresult   *last_ins; /* Last insert result */
	static int  rec_count;     /* Number of records in last query */
	static int  row;
	static int  col;
	bool *sql_init = false;
} sql;

/* Filesystem Settings */
struct {
	char mount_point[255];
} settings;

/* Layout version */
#define hifs_LAYOUT_VER		1

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
#define HIFS_BAD_INO		1 /* Bad blocks inode */
#define HIFS_ROOT_INO		2 /* Root inode nr */
#define HIFS_LAF_INO		3 /* Lost and Found inode nr */
#define EXT4_USR_QUOTA_INO	 3	/* User quota inode */
#define EXT4_GRP_QUOTA_INO	 4	/* Group quota inode */
#define EXT4_BOOT_LOADER_INO 5	/* Boot loader inode */
#define EXT4_UNDEL_DIR_INO	 6	/* Undelete directory inode */
#define EXT4_RESIZE_INO		 7	/* Reserved group descriptors inode */
#define EXT4_JOURNAL_INO	 8	/* Journal inode */



/** 
 *Filesystem definition 
 **/
static struct file_system_type hifs_type = {
    .name = "hifs",
    .mount = hifs_mount,
    .kill_sb = kill_block_super,
};

/**
 * The on-disk superblock - last 3 vars synced w/ hive queen.
 **/
struct hifs_superblock {
	uint32_t	s_magic;    	/* magic number */
	uint32_t	s_version;    	/* fs version */
	uint32_t	s_blocksize;	/* fs block size */
	uint32_t	s_block_olt;	/* Object location table block */
	uint32_t	s_inode_cnt;	/* number of inodes in inode table */
	uint32_t	s_last_blk;	    /* just move forward with allocation */
};

/**
 * Object Location Table
 **/
struct hifs_olt {
	uint32_t	inode_table;		/* inode_table block location */
	uint32_t	inode_cnt;	     	/* number of inodes */
	uint64_t	inode_bitmap;		/* inode bitmap block */
};

/**
 * The on Disk inode
 **/
struct hifs_inode {
	uint8_t		i_version;	/* inode version */
	uint8_t		i_flags;	/* inode flags: TYPE */
	uint32_t	i_mode;		/* File mode */
	uint32_t	i_ino;		/* inode number */
	uint16_t	i_uid;		/* owner's user id */
	uint16_t	i_hrd_lnk;	/* number of hard links */
	uint32_t 	i_ctime;	/* Creation time */
	uint32_t	i_mtime;	/* Modification time*/
	uint32_t	i_size;		/* Number of bytes in file */
	/* address begin - end block, range exclusive: addres end (last block) does not belogs to extend! */
	uint32_t	i_addrb[HIFS_INODE_TSIZE];	/* Start block of extend ranges */
	uint32_t	i_addre[HIFS_INODE_TSIZE];	/* End block of extend ranges */
};

struct hifs_dir_entry {
	uint32_t inode_nr;		/* inode number */
	uint32_t name_len;		/* Name length */
	char name[256];			/* File name, up to HIFS_NAME_LEN */
};

 /*
  * Turn on EXT_DEBUG to enable ext4_ext_show_path/leaf/move in extents.c
  */
#define EXT_DEBUG__

#endif /* _LINUX_HIVEFS_H */

#ifdef HIVEFS_DEBUG
#define hifs_debug(f, a...)						\
	do {								\
		printk(KERN_DEBUG "hive-fs DEBUG (%s, %d): %s:",	\
			__FILE__, __LINE__, __func__);			\
		printk(KERN_DEBUG f, ## a);				\
	} while (0)
#else
#define hifs_debug(fmt, ...)	no_printk(fmt, ##__VA_ARGS__)
#endif /* _HIVEFS_DEBUG */