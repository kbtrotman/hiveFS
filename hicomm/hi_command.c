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
extern char device_file_inode[256];
extern char device_file_block[256];
extern char device_file_cmd[50];

extern char buffer[4096];

struct hifs_inode *shared_inode_outgoing;    // These six Doubly-Linked Lists are our
struct hifs_blocks *shared_block_outgoing;   // processing queues. They are sent & 
struct hifs_cmds *shared_cmd_outgoing;       // received thru the 3 device files known
struct hifs_inode *shared_inode_incoming;    // as the "queues" (to hi_command). We want
struct hifs_blocks *shared_block_incoming;   // to proces them fast, so they're split into
struct hifs_cmds *shared_cmd_incoming;       // incoming & outgoing queues here.
 

int main(int argc, char *argv[])
{
    hifs_user_link.clockstart = GET_TIME();
    strcpy(atomic_path, "/dev/");
    strcpy(atomic_device_name, atomic_path);
    strcat(atomic_device_name, atomic_device);

    strcpy(device_file_inode, atomic_path);
    strcat(device_file_inode, DEVICE_FILE_INODE);

    strcpy(device_file_block, atomic_path);
    strcat(device_file_block, DEVICE_FILE_BLOCK);

    strcpy(device_file_cmd, atomic_path);
    strcat(device_file_cmd, DEVICE_FILE_CMDS);

    int atomic_value;
    hi_comm_queue_init(); 
    atomic_value = read_from_atomic();
    printf("hi-command: Atomic value: %d\n", atomic_value);

    init_hive_link();


 queue_management:
    // Here we ignore values 1,3,4,8,10   
    if (atomic_value == HIFS_Q_PROTO_UNUSED) {
        write_to_atomic(3);
        printf("hi-command: Atomic value: %d\n", atomic_value);
        scan_user_queue_and_send();
    }
    else if (atomic_value == HIFS_Q_PROTO_KERNEL_WO_USER) {
        write_to_atomic(3);
        printf("hi-command: Atomic value: %d\n", atomic_value);
        scan_user_queue_and_recv();    
    }
    else if (atomic_value == HIFS_Q_PROTO_ACK_LINK_KERN) {
        printf("hi-command: Received hivefs Link_Up Command to user-space.\n");
        hifs_user_link.last_state = hifs_user_link.state;
        hifs_user_link.state = HIFS_COMM_LINK_UP;
        printf("hi-command: user link up'd at %ld seconds after hi_command start.\n", (GET_TIME() - hifs_user_link.clockstart));
        hifs_user_link.last_check = 0;
        write_to_atomic(8);
        printf("hi-command: Atomic value: %d\n", atomic_value);
    }
    else {
        printf("hi-command: Atomic value: %d\n", atomic_value);
    }

    goto queue_management;
    
}