/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "hi_command.h"

// Globals
const char *kern_atomic_device = ATOMIC_KERN_DEVICE_NAME;
const char *user_atomic_device = ATOMIC_USER_DEVICE_NAME;
char kern_atomic_device_name[256];
char user_atomic_device_name[256];
char kern_atomic_path[20];
char user_atomic_path[20];
int kern_atomic_value;
int user_atomic_value;

struct hifs_link hifs_user_link;
struct hifs_link hifs_kern_link;

int ret, fd_cmd, fd_inode, fd_block;
char buffer[4096];
char *device_file_inode;
char *device_file_block;
char *device_file_cmd;

struct hifs_inode *shared_inode_outgoing;    // These six Doubly-Linked Lists are our
struct hifs_blocks *shared_block_outgoing;   // processing queues. They are sent & 
struct hifs_cmds *shared_cmd_outgoing;       // received thru the 3 device files known
struct hifs_inode *shared_inode_incoming;    // as the "queues" (to hi_command). We want
struct hifs_blocks *shared_block_incoming;   // to proces them fast, so they're split into
struct hifs_cmds *shared_cmd_incoming;       // incoming & outgoing queues here.

struct list_head shared_inode_outgoing_lst;    
struct list_head shared_block_outgoing_lst;    
struct list_head shared_cmd_outgoing_lst;       
struct list_head shared_inode_incoming_lst;    
struct list_head shared_block_incoming_lst;   
struct list_head shared_cmd_incoming_lst;  


int read_from_atomic(enum hifs_module program)
{
    int fd, value;
    char device_name[256];
    if (program == HIFS_COMM_PROGRAM_KERN_MOD) {
        strcpy(device_name, kern_atomic_device_name);
    } else {
        strcpy(device_name, user_atomic_device_name);
    }
    fd = open(device_name, O_RDWR);
    if (fd == -1) {
        perror("hi_command: Error opening device file");
        return -1;
    }

    // Reading from the atomic variable
    read(fd, &value, sizeof(value));
    printf("hi_command: Read atomic link status from [%s]: %d\n", device_name, value);
    close(fd);
    return value;
}

int write_to_atomic(int value, enum hifs_module program)
{
    int fd;
    char device_name[256];
    if (program == HIFS_COMM_PROGRAM_KERN_MOD) {
        strcpy(device_name, kern_atomic_device_name);
    } else {
        strcpy(device_name, user_atomic_device_name);
    }
    fd = open(device_name, O_RDWR);
    if (fd == -1) {
        perror("hi_command: Error opening device file");
        return -1;
    }

    // Writing to the atomic variable in the kernel space
    ret = write(fd, &value, sizeof(value));
    printf("hi_command: Wrote link status to [%s]: %d\n", device_name, value);

    close(fd);
    return value;
}

void hi_comm_close_queues(void)
{
    //free_a_list(&shared_inode_outgoing_lst, struct hifs_inode, hifs_inode_list);
    //free_a_list(&shared_block_outgoing_lst, struct hifs_blocks, hifs_block_list);
    //free_a_list(&shared_cmd_outgoing_lst, struct hifs_cmds, hifs_cmd_list);
    //free_a_list(&shared_inode_incoming_lst, struct hifs_inode, hifs_inode_list);
    //free_a_list(&shared_block_incoming_lst, struct hifs_blocks, hifs_block_list);
    //free_a_list(&shared_cmd_incoming_lst, struct hifs_cmds, hifs_cmd_list);
    munmap(shared_inode_incoming, sizeof(struct hifs_inode));
    munmap(shared_block_incoming, sizeof(struct hifs_blocks));
    munmap(shared_cmd_incoming, sizeof(struct hifs_cmds));
    close(fd_inode);
    close(fd_block);
    close(fd_cmd);
}

int read_from_inode_dev(char *dev_file)
{
    int result = 0;
    struct hifs_inode_user send_data_user;

    result = read(fd_cmd, &send_data_user, sizeof(struct hifs_inode_user));
    if (result >= 0) {

        strncpy(shared_inode_incoming->i_name, send_data_user.i_name, sizeof(HIFS_MAX_NAME_SIZE));
        memcpy(shared_inode_incoming->i_addre, send_data_user.i_addre, sizeof(shared_inode_incoming->i_addre));
        memcpy(shared_inode_incoming->i_addrb, send_data_user.i_addrb, sizeof(shared_inode_incoming->i_addrb));
        shared_inode_incoming->i_size = send_data_user.i_size;
        shared_inode_incoming->i_mode = send_data_user.i_mode;
        shared_inode_incoming->i_uid = send_data_user.i_uid;
        shared_inode_incoming->i_gid = send_data_user.i_gid;
        shared_inode_incoming->i_blocks = send_data_user.i_blocks;
        shared_inode_incoming->i_bytes = send_data_user.i_bytes;
        shared_inode_incoming->i_size = send_data_user.i_size;
        shared_inode_incoming->i_ino = send_data_user.i_ino;

        printf("hi_command: Read %d bytes from device file: %s\n", result, dev_file);
        printf("hi_command: Received inode [%s] with [%d] file size\n", shared_inode_incoming->i_name, shared_inode_incoming->i_bytes);
    } else {
        // Handle error
        perror("hi_command: Error reading from command device file");
        return result;
    }

    result = sizeof(*shared_inode_incoming);
    list_add(&shared_inode_incoming->hifs_inode_list, &shared_inode_incoming_lst);

    return result;
}

int read_from_block_dev(char *dev_file)
{
    int result = 0;

    struct hifs_blocks_user {
        char block[HIFS_DEFAULT_BLOCK_SIZE];
        int block_size;
        int count;
    };
    struct hifs_blocks_user *send_data_user = mmap(0, sizeof(struct hifs_blocks_user), PROT_READ | PROT_WRITE, MAP_SHARED, fd_block, 0);
    result = read(fd_cmd, &send_data_user, sizeof(struct hifs_blocks_user));
    if (result >= 0) {
        strncpy(shared_block_incoming->block, send_data_user->block, HIFS_DEFAULT_BLOCK_SIZE);
        shared_block_incoming->block_size = send_data_user->block_size;

        printf("hi_command: Read %d bytes from device file: %s\n", result, dev_file);
        printf("hi_command: Received block [%s] with [%d] block size\n", shared_block_incoming->block, shared_block_incoming->block_size);
    } else {
        // Handle error
        perror("hi_command: Error reading from command device file");
        return result;
    }
    result = sizeof(*shared_block_incoming);
    list_add(&shared_block_incoming->hifs_block_list, &shared_block_incoming_lst);

    return result;
}

int read_from_cmd_dev(char *dev_file)
{
    int result = 0;
    struct hifs_cmds_user {
        char cmd[HIFS_MAX_CMD_SIZE];
        int count;
    };
     struct hifs_cmds_user send_data_user;
    result = read(fd_cmd, &send_data_user, sizeof(struct hifs_cmds_user));
    if (result >= 0) {
        send_data_user.cmd[sizeof(struct hifs_cmds_user) - 1] = '\0';
        strncpy(shared_cmd_incoming->cmd, send_data_user.cmd, sizeof(HIFS_MAX_CMD_SIZE));
        shared_cmd_incoming->count = send_data_user.count;
        printf("hi_command: Read %d bytes from device file: %s\n", result, dev_file);
        printf("hi_command: Received command [%s] with [%d] count\n", shared_cmd_incoming->cmd, shared_cmd_incoming->count);
    } else {
        // Handle error
        perror("hi_command: Error reading from command device file");
        return result;
    }

    result = sizeof(*shared_cmd_incoming);
    list_add(&shared_cmd_incoming->hifs_cmd_list, &shared_cmd_incoming_lst);

    return result;
}

int write_to_inode_dev(void)
{
    // Write out to user space.
    int result = 0;
    struct hifs_inode *send_data = NULL;
    struct hifs_inode_user send_data_user;

    printf("hi_command: [INODE] popping an item from the queue to send\n");
    if (!list_empty(&shared_inode_outgoing_lst)) { 
        send_data = list_entry(&shared_inode_outgoing_lst, struct hifs_inode, hifs_inode_list);
        if (send_data) {
            list_del(&send_data->hifs_inode_list);
        } else {
            printf("hi_command: [INODE] send_data is empty, dropping out to process next queue message\n");
            return -EFAULT;
        }
        printf("hi_command: [INODE] Transferring inode [%s]\n", send_data->i_name);
        strncpy(send_data_user.i_name, send_data->i_name, HIFS_MAX_NAME_SIZE);
        memcpy(send_data_user.i_addre, send_data->i_addre, sizeof(send_data_user.i_addre));
        memcpy(send_data_user.i_addrb, send_data->i_addrb, sizeof(send_data_user.i_addrb));
        send_data_user.i_size = send_data->i_size;
        send_data_user.i_mode = send_data->i_mode;
        send_data_user.i_uid = send_data->i_uid;
        send_data_user.i_gid = send_data->i_gid;
        send_data_user.i_blocks = send_data->i_blocks;
        send_data_user.i_bytes = send_data->i_bytes;
        send_data_user.i_size = send_data->i_size;
        send_data_user.i_ino = send_data->i_ino;
        ret = write(fd_inode, &send_data_user, sizeof(send_data_user));
        if (ret == -1) {
            perror("hi_command: Error writing to device file");
            return -1;
        } else {
            printf("hi_command: Wrote %d bytes to device file: %s\n", ret, device_file_cmd);
        }
    } else {
        result = -EFAULT;
    }
    free(send_data);
    return result;
}

int write_to_block_dev(void)
{
    struct hifs_block_user {
        char block[HIFS_DEFAULT_BLOCK_SIZE];  // MAX_CMD_SIZE is the maximum size of a command
        int block_size;
    };
    int result = 0;
    struct hifs_blocks *send_data = NULL;
    struct hifs_block_user *send_data_user = mmap(0, sizeof(struct hifs_block_user), PROT_READ | PROT_WRITE, MAP_SHARED, fd_block, 0);
    printf("hi_command: [BLOCK] popping an item from the queue to send\n");
    if (!list_empty(&shared_block_outgoing_lst)) { 
        send_data = list_entry(&shared_block_outgoing_lst, struct hifs_blocks, hifs_block_list);
        if (send_data) {
            list_del(&send_data->hifs_block_list);
        } else {
            printf("hi_command: [BLOCK] send_data is empty, dropping out to process next queue message\n");
            return -EFAULT;
        }
        printf("hi_command: [BLOCK] Transferring block [%s] with [%d] characters\n", send_data->block, send_data->block_size);
        strncpy(send_data_user->block, send_data->block, HIFS_DEFAULT_BLOCK_SIZE);
        send_data_user->block_size = send_data->block_size;
        ret = write(fd_block, &send_data_user, sizeof(send_data_user));
        if (ret == -1) {
            perror("hi_command: Error writing to device file");
            return -1;
        } else {
            printf("hi_command: Wrote %d bytes to device file: %s\n", ret, device_file_cmd);
        }
    } else {
        result = -EFAULT;
    }
    free(send_data);
    return result;
}

int write_to_cmd_dev(void)
{
    // Write out to user space.
    int result = 0;
    struct hifs_cmds_user {
        char cmd[HIFS_MAX_CMD_SIZE];
        int count;
    };
    struct hifs_cmds *send_data = NULL;
    struct hifs_cmds_user send_data_user;
    printf("hi_command: [CMD] popping an item from the queue to send\n");
    if (!list_empty(&shared_cmd_outgoing_lst)) { 
        send_data = list_entry(&shared_cmd_outgoing_lst, struct hifs_cmds, hifs_cmd_list);
        if (send_data) {
            list_del(&send_data->hifs_cmd_list);
        } else {
            printf("hi_command: [CMD] send_data is empty, dropping out to process next queue message\n");
            return -EFAULT;
        }
        printf("hi_command: [CMD] Transferring command [%s] with [%d] characters\n", send_data->cmd, send_data->count);
        strncpy(send_data_user.cmd, send_data->cmd, HIFS_MAX_CMD_SIZE);
        send_data_user.count = send_data->count;
        ret = write(fd_cmd, &send_data_user, sizeof(send_data_user));
        if (ret == -1) {
            perror("hi_command: Error writing to device file");
            return -1;
        } else {
            printf("hi_command: Wrote %d bytes to device file: %s\n", ret, device_file_cmd);
        }

    } else {
        result = -EFAULT;
    }
    free(send_data);
    return result;
}