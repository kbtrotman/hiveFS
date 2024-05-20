/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"

//Globals
extern const char *kern_atomic_device;
extern const char *user_atomic_device;
extern char kern_atomic_device_name[256];
extern char user_atomic_device_name[256];
extern char kern_atomic_path[20];
extern char user_atomic_path[20];
extern int kern_atomic_value;
extern int user_atomic_value;

extern struct hifs_link hifs_user_link;
extern struct hifs_link hifs_kern_link;

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

void write_to_queue(void)
{
    int ret;
    printf("hi-command: Writing to queue.\n");
    while (!list_empty(&shared_cmd_outgoing_lst)) {
        ret = write_to_cmd_dev();
        if (ret < 0) {
            printf("hi-command: Error writing to device file: %s\n", device_file_cmd);
            return;
        } else if (ret == 0) {
            // Just keep cycling until we have data to send, ending up here means the queue is empty right now.
            printf("hi-command: The send queues are empty, waiting and checking...\n");
            return;
        } else {
            printf("hi-command: Wrote %d bytes to device file: %s\n", ret, device_file_cmd);
            printf("\n");
        }
    }
    return;
}

void read_from_queue(void)
{
    int ret;
    printf("hi-command: Reading from queue...\n");
    ret = read_from_cmd_dev(device_file_cmd);
    if (ret < 0) {
        printf("hi-command: Error reading from device file: %s\n", device_file_cmd);
        return;
    } else {
        printf("hi-command: Read %d bytes from device file: %s\n", ret, device_file_cmd);
        // Which Queues (Inode & Block) are used is dependant on the command just sent in the Command Queue
        if (strcmp(shared_cmd_incoming->cmd, HIFS_Q_PROTO_CMD_TEST) == 0) {
            ret = read_from_cmd_dev(device_file_block);
            ret = read_from_cmd_dev(device_file_inode);
        }
        
    }
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

    shared_inode_outgoing = malloc(sizeof(*shared_inode_outgoing));
    shared_cmd_outgoing = malloc(sizeof(*shared_cmd_outgoing));
    shared_block_outgoing = malloc(sizeof(*shared_block_outgoing));   

    if (!shared_inode_outgoing || !shared_cmd_outgoing || !shared_block_outgoing) {
        if (shared_cmd_outgoing) { free(shared_cmd_outgoing); }
        if (shared_inode_outgoing) { free(shared_inode_outgoing); }
        if (shared_block_outgoing) { free(shared_block_outgoing); }
        return -1;
    }

    *shared_inode_outgoing = (struct hifs_inode) {
        .i_mode = 99,
        .i_uid = 99,
        .i_gid = 99,
        .i_blocks = 99,
        .i_bytes = 99,
        .i_size = 99,
        .i_ino = 99,
        .i_name = "my_test",
    };

    *shared_cmd_outgoing = (struct hifs_cmds){
        .cmd = HIFS_Q_PROTO_CMD_TEST,
        .count = 1,
    };

    *shared_block_outgoing = (struct hifs_blocks) {
        .block_size = 84,
        .count = 1,
        .block = "test_block_data:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:test_block_data",

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
    list_add_tail(&shared_block_outgoing->hifs_block_list, &shared_block_outgoing_lst);
    return 0;
}

int hifs_comm_check_program_up(int program) {
    int value;
    if (program == HIFS_COMM_PROGRAM_KERN_MOD) {
        value = read_from_atomic(HIFS_COMM_PROGRAM_KERN_MOD);
    } else if (program == HIFS_COMM_PROGRAM_USER_HICOMM) {
        value = read_from_atomic(HIFS_COMM_PROGRAM_USER_HICOMM);
    }

    return value;
}

int hifs_comm_set_program_up( int program ) {
    int value;
    if (program == HIFS_COMM_PROGRAM_KERN_MOD) {
        // Hi_Command should never set this mem location. It's owned by the kernel.
        value = hifs_comm_check_program_up(HIFS_COMM_PROGRAM_KERN_MOD);
        if (value == HIFS_COMM_LINK_UP) {
            hifs_kern_link.last_state = hifs_kern_link.state;
            hifs_kern_link.state = HIFS_COMM_LINK_UP;
            hifs_kern_link.last_check = 0;
        } else {
            hifs_kern_link.last_state = hifs_kern_link.state;
            hifs_kern_link.state = HIFS_COMM_LINK_DOWN;
            hifs_kern_link.last_check = 0; 
        }
        printf("hi_command: kern link up'd at %ld seconds after hi_command start, waiting on hi_command.\n", (GET_TIME() - hifs_kern_link.clockstart));
    } else if (program == HIFS_COMM_PROGRAM_USER_HICOMM) {
        //...The kernel should never set this mem location. It's owned by user space.
        write_to_atomic(HIFS_COMM_LINK_UP, HIFS_COMM_PROGRAM_USER_HICOMM);
        hifs_user_link.last_state = hifs_user_link.state;
        hifs_user_link.state = HIFS_COMM_LINK_UP;
        hifs_user_link.last_check = 0;
        value = 1;
        printf("hi_command: user link up'd at %ld seconds after hifs start, waiting on kernel if applicable.\n", (GET_TIME() - hifs_user_link.clockstart));
    } else {
        value = 0;
    }

    return value;
}

int hifs_comm_set_program_down(int program) {
    int value;
    if (program == HIFS_COMM_PROGRAM_KERN_MOD) {  
        //...User Space should never set this mem location. It's owned by kernel space.
        value = hifs_comm_check_program_up(HIFS_COMM_PROGRAM_KERN_MOD);
        if (value == HIFS_COMM_LINK_UP) {
            // We don't want to do this! The Kernel Module has to flush and shutdown the kernel side of the link! Do nothing.
            value = 1;
        }
        printf("hi_command: kern link down attempted and rejected at %ld seconds after hi_command start, waiting on kernel.\n", (GET_TIME() - hifs_kern_link.clockstart));
    } else if (program == HIFS_COMM_PROGRAM_USER_HICOMM) {
        write_to_atomic(HIFS_COMM_LINK_UP, HIFS_COMM_PROGRAM_USER_HICOMM);
        hifs_user_link.last_state = hifs_user_link.state;
        hifs_user_link.last_check = 0;
        hifs_user_link.state = HIFS_COMM_LINK_UP;
        value = 1;
        printf("hi_command: user link up'd at %ld seconds after hi_command start, waiting on kernel if applicable.\n", (GET_TIME() - hifs_user_link.clockstart));
    } else {
        value = 0;
    }

    return value;
}