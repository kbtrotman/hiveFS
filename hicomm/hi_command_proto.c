/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"


extern char atomic_device_name[256];  // Make sure this is large enough
extern char *device_file_inode;
extern char *device_file_block;
extern char *device_file_cmd;
extern char buffer[4096];

extern struct hifs_inode *shared_inode_outgoing;    // These six Doubly-Linked Lists are our
extern struct hifs_blocks *shared_block_outgoing;   // processing queues. They are sent & 
extern struct hifs_cmds *shared_cmd_outgoing;       // received thru the 3 device files known
extern struct hifs_inode *shared_inode_incoming;    // as the "queues" (to hi_command). We want
extern struct hifs_blocks *shared_block_incoming;   // to proces them fast, so they're split into
extern struct hifs_cmds *shared_cmd_incoming;       // incoming & outgoing queues here.

extern struct list_head shared_inode_outgoing_lst;    
extern struct list_head shared_block_outgoing_lst;    
extern struct list_head shared_cmd_outgoing_lst;       
extern struct list_head shared_inode_incoming_lst;    
extern struct list_head shared_block_incoming_lst;   
extern struct list_head shared_cmd_incoming_lst;  

extern char *filename;     // The filename we're currently sending/recieving to/from.

void write_to_queue(void)
{
    int ret;
    while (!list_empty(&shared_cmd_outgoing_lst)) {
        ret = write_to_cmd_dev();
        if (ret < 0) {
            printf("hi-command: Error writing to device file: %s\n", device_file_cmd);
            return;
        } else {
            printf("hi-command: Wrote %d bytes to device file: %s\n", ret, device_file_cmd);
            printf("\n");
        }
    }
    //Save data to the outgoing queue.

    return;
}

void read_from_queue(void)
{
    int ret;

    ret = read_from_cmd_dev(device_file_cmd);
    if (ret < 0) {
        printf("hi-command: Error reading from device file: %s\n", device_file_cmd);
        return;
    } else {
        printf("hi-command: Read %d bytes from device file: %s\n", ret, device_file_cmd);
    }
    printf("\n");

    return;
}

int hifs_init_queues(void) {

    shared_inode_incoming = malloc(sizeof(*shared_inode_incoming));
    shared_cmd_incoming = malloc(sizeof(*shared_cmd_incoming));
    shared_block_incoming = malloc(sizeof(*shared_block_incoming));

    if (!shared_inode_incoming || !shared_cmd_incoming || !shared_block_incoming) {
        if (shared_cmd_incoming) { free(shared_cmd_incoming); }
        if (shared_inode_incoming) { free(shared_inode_incoming); }
        if (shared_block_incoming) { free(shared_block_incoming); }
        return -1;
    }

    shared_inode_outgoing = malloc(sizeof(*shared_inode_incoming));
    shared_cmd_outgoing = malloc(sizeof(*shared_cmd_incoming));
    shared_block_outgoing = malloc(sizeof(*shared_block_incoming));   

    if (!shared_inode_outgoing || !shared_cmd_outgoing || !shared_block_outgoing) {
        if (shared_cmd_outgoing) { free(shared_cmd_outgoing); }
        if (shared_inode_outgoing) { free(shared_inode_outgoing); }
        if (shared_block_outgoing) { free(shared_block_outgoing); }
        return -1;
    }

    *shared_inode_outgoing = (struct hifs_inode) {
        .i_mode = 0,
        .i_uid = 0,
        .i_gid = 0,
        .i_blocks = 0,
        .i_bytes = 0,
        .i_size = 0,
        .i_ino = 0,
    };

    *shared_cmd_outgoing = (struct hifs_cmds){
        .cmd = NULL,
        .count = 0,
    };

    INIT_LIST_HEAD(&shared_inode_incoming->hifs_inode_list);
    INIT_LIST_HEAD(&shared_cmd_incoming->hifs_cmd_list);
    INIT_LIST_HEAD(&shared_block_incoming->hifs_block_list);
    INIT_LIST_HEAD(&shared_inode_outgoing->hifs_inode_list);
    INIT_LIST_HEAD(&shared_cmd_outgoing->hifs_cmd_list);
    INIT_LIST_HEAD(&shared_block_outgoing->hifs_block_list);

    INIT_LIST_HEAD(&shared_inode_outgoing_lst);
    INIT_LIST_HEAD(&shared_block_outgoing_lst);
    INIT_LIST_HEAD(&shared_cmd_outgoing_lst);
    INIT_LIST_HEAD(&shared_inode_incoming_lst);
    INIT_LIST_HEAD(&shared_block_incoming_lst);
    INIT_LIST_HEAD(&shared_cmd_incoming_lst);

    list_add_tail(&shared_cmd_outgoing->hifs_cmd_list, &shared_cmd_outgoing_lst);
    list_add_tail(&shared_inode_outgoing->hifs_inode_list, &shared_inode_outgoing_lst);

    return 0;
}