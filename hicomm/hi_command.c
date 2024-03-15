/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"

struct hifs_link hifs_user_link;
extern const char *atomic_device;
extern char atomic_path[20];
extern char atomic_device_name[256]; 
extern char *device_file_inode;
extern char *device_file_block;
extern char *device_file_cmd;

extern char buffer[4096];
extern struct PSQL sqldb;

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


int main(int argc, char *argv[])
{
    int atomic_value;

    hifs_user_link.clockstart = GET_TIME();
    sqldb.hive_conn = NULL;
    sqldb.last_qury = NULL;
    sqldb.last_ins = NULL;
    sqldb.col = 0;
    sqldb.row = 0;
    sqldb.rec_count = 0;
    sqldb.sql_init = false;

    device_file_inode = malloc(256);
    device_file_block = malloc(256);
    device_file_cmd = malloc(50);

    strcpy(atomic_path, "/dev/");
    strcpy(atomic_device_name, atomic_path);
    strcat(atomic_device_name, atomic_device);

    strcpy(device_file_inode, atomic_path);
    strcat(device_file_inode, DEVICE_FILE_INODE);

    strcpy(device_file_block, atomic_path);
    strcat(device_file_block, DEVICE_FILE_BLOCK);

    strcpy(device_file_cmd, atomic_path);
    strcat(device_file_cmd, DEVICE_FILE_CMDS);

    init_hive_link();


 queue_management:
    atomic_value = read_from_atomic();
    printf("hi-command: Atomic value: %d\n", atomic_value);
    // Here we ignore values 1,3,4,8,10   

    if (atomic_value == HIFS_Q_PROTO_ACK_LINK_KERN || hifs_user_link.state == HIFS_COMM_LINK_DOWN) {
        //printf("hi-command: Received hivefs Link_Up Command to user-space.\n");
        hifs_user_link.last_state = hifs_user_link.state;
        hifs_user_link.state = HIFS_COMM_LINK_UP;
        printf("hi-command: user link up'd at %ld seconds after hi_command start.\n", (GET_TIME() - hifs_user_link.clockstart));
        hifs_user_link.last_check = 0;
        if (atomic_value == HIFS_Q_PROTO_ACK_LINK_KERN) {
            write_to_atomic(HIFS_Q_PROTO_UNUSED);
        } else if (hifs_user_link.state == HIFS_COMM_LINK_DOWN) {
            write_to_atomic(HIFS_Q_PROTO_ACK_LINK_USER);
        }
    }

    // Send to the queues: IE Write
        // If list isn't empty...
        // If file isn't locked...
        // Pop a list entry and write it.
    // Read from the queues: IE Read
        // If list isn't full...
        // If file isn't locked...
        // Read and place entry on lists...

    goto queue_management;
    
}