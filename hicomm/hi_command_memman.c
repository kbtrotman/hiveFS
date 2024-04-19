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

int ret, fd_cmd, fd_inode, fd_block, atomic_value;

char buffer[4096];

const char *atomic_device = ATOMIC_DEVICE_NAME;
char atomic_device_name[256];
char atomic_path[20];
char *device_file_inode;
char *device_file_block;
char *device_file_cmd;

char buffer[4096];
char *filename;
struct hifs_link hifs_user_link;

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


int read_from_atomic(void)
{
    int fd;
    int value;

    fd = open(atomic_device_name, O_RDWR);
    if (fd == -1) {
        perror("hi_command: Error opening device file");
        return -1;
    }

    // Reading from the atomic variable in the kernel space
    read(fd, &value, sizeof(int));
    printf("hi_command: Read value from kernel: %d\n", value);
    close(fd);
    return value;
}

int write_to_atomic(int value)
{
    int fd, ret;

    fd = open(atomic_device_name, O_RDWR);
    if (fd == -1) {
        perror("hi_command: Error opening device file");
        return -1;
    }

    // Writing to the atomic variable in the kernel space
    ret = write(fd, &value, sizeof(int));
    printf("hi_command: Wrote value to kernel: %d\n", value);

    close(fd);
    return ret;
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

    result = read(fd_cmd, &shared_inode_incoming, sizeof(shared_inode_incoming));
    if (result >= 0) {
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

    result = read(fd_cmd, &shared_block_incoming, sizeof(shared_block_incoming));
    if (result >= 0) {
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
    result = read(fd_cmd, &shared_cmd_incoming, sizeof(shared_cmd_incoming));
    if (result >= 0) {
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