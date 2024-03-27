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
    int i;
    struct hifs_cmds *outgoing_cmd;
    outgoing_cmd = malloc(sizeof(struct hifs_cmds)); // allocate memory
    if (outgoing_cmd == NULL) {
        printf("hi-command: Memory couldn't be allocated for outgoing command queue\n");
        return;
    }
    strcpy(outgoing_cmd->cmd, HIFS_Q_PROTO_CMD_TEST);
    ret = write_to_cmd_dev(outgoing_cmd->cmd, sizeof(struct hifs_cmds));
    if (ret < 0) {
        printf("hi-command: Error writing to device file: %s\n", device_file_cmd);
        return;
    } else {
        printf("hi-command: Wrote %d bytes to device file: %s\n", ret, device_file_cmd);
        for (i = 0; i < ret; i++) {
            printf("%c", outgoing_cmd->cmd[i]);
        }
        printf("\n");
    }

    //Save data to the outgoing queue.

    return;

}

void read_from_queue(void)
{
    int ret;
    int i;
    struct hifs_cmds *incoming_cmd;
    incoming_cmd = malloc(sizeof(struct hifs_cmds)); // allocate memory
    if (incoming_cmd == NULL) {
        printf("hi-command: Memory couldn't be allocated for incoming command queue\n");
        return;
    }
    ret = read_from_cmd_dev(device_file_cmd, sizeof(struct hifs_cmds));
    if (ret < 0) {
        printf("hi-command: Error reading from device file: %s\n", device_file_cmd);
        return;
    } else {
        printf("hi-command: Read %d bytes from device file: %s\n", ret, device_file_cmd);
        for (i = 0; i < ret; i++) {
            incoming_cmd->cmd[i] = shared_cmd_incoming->cmd[i];
            printf("%c", incoming_cmd->cmd[i]);
            printf("\n");
        }

        if (strcmp(incoming_cmd->cmd, HIFS_Q_PROTO_CMD_TEST) == 0) {
            printf("hi-command: Received test command\n");
            ret = read_from_inode_dev(device_file_inode, sizeof(struct hifs_inode));
            if (ret < 0) {
                printf("hi-command: Error reading from device file: %s\n", device_file_inode);
                return;
            } else {
                printf("hi-command: Read %d bytes from device file: %s\n", ret, device_file_inode);
                for (i = 0; i < ret; i++) {
                    printf("%d", shared_inode_incoming->i_name[i]);
                }
                printf("\n");
            }
        }
    }

    free(incoming_cmd);
    //Save data to the incoming queue.

    return;
}