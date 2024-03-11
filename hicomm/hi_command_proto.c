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

void scan_user_queue_and_send(void)
{



    write_to_atomic(4);
    return;
}

void scan_user_queue_and_recv(void)
{
    read_from_command_queue();
    // We have the data, so acknowledge to the kernel.
    write_to_atomic(0);
    return;
}

void read_from_command_queue(void)
{
    int ret;
    int i;
    struct hifs_cmds *incoming_cmd;
    incoming_cmd = malloc(sizeof(incoming_cmd)); // allocate memory
    if (incoming_cmd == NULL) {
        printf("hi-command: Memory couldn't be allocated for incoming command queue\n");
        return;
    }
    ret = read_from_dev(device_file_cmd, sizeof(buffer));
    if (ret < 0) {
        return;
    } else {
        printf("hi-command: Read %d bytes from device file: %s\n", ret, device_file_cmd);
        for (i = 0; i < ret; i++) {
            incoming_cmd->cmd[i] = buffer[i];
            if (buffer[i] == '\0') {
                break;
            }
            printf("%c", buffer[i]);
        }
        printf("\n");

        if (strcmp(incoming_cmd->cmd, HIFS_Q_PROTO_CMD_TEST) == 0) {
            printf("hi-command: Received test command\n");
            ret = read_from_dev(device_file_inode, sizeof(buffer));
            if (ret < 0) {
                printf("hi-command: Error reading from device file: %s\n", device_file_inode);
                return;
            } else {
                printf("hi-command: Read %d bytes from device file: %s\n", ret, device_file_inode);
                for (i = 0; i < ret; i++) {
                    printf("%c", buffer[i]);
                }
                printf("\n");
            }
        }
    }


    //Save data to the incoming queue.

    return;
}