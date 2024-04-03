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

int ret, fd_cmd, fd_inode, fd_block;

char buffer[4096];

const char *atomic_device = ATOMIC_DEVICE_NAME;
char atomic_device_name[256];
char atomic_path[20];
char *device_file_inode;
char *device_file_block;
char *device_file_cmd;

char buffer[4096];
char *filename;

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
        perror("hi_comm: Error opening device file");
        return -1;
    }

    // Reading from the atomic variable in the kernel space
    read(fd, &value, sizeof(int));
    printf("hi_comm: Read value from kernel: %d\n", value);
    close(fd);
    return value;
}

int write_to_atomic(int value)
{
    int fd, ret;

    fd = open(atomic_device_name, O_RDWR);
    if (fd == -1) {
        perror("hi_comm: Error opening device file");
        return -1;
    }

    // Writing to the atomic variable in the kernel space
    ret = write(fd, &value, sizeof(int));
    printf("hi_comm: Wrote value to kernel: %d\n", value);

    close(fd);
    return ret;
}

void hi_comm_close_queues(void)
{
    munmap(shared_inode_incoming, sizeof(struct hifs_inode));
    munmap(shared_block_incoming, HIFS_DEFAULT_BLOCK_SIZE);
    munmap(shared_cmd_incoming, sizeof(struct hifs_inode));
    close(fd_inode);
    close(fd_block);
    close(fd_cmd);
}

int read_from_inode_dev(char *dev_file, int size)
{
    int ret;

    shared_inode_incoming = mmap(0, sizeof(shared_inode_incoming), PROT_READ | PROT_WRITE, MAP_SHARED, fd_inode, 0);
    if (!shared_inode_incoming) {
        perror("hi_comm: Error mapping shared memory");
        return -1;
    }
    ret = read(fd_inode, &shared_inode_incoming, size);
    if (ret >= 0) {
        //shared_inode_incoming[ret] = '\0';  // null-terminate the string, read doesn't play well.
    } else {
        // Handle error
        perror("hi_comm: Error reading from inode device file");
        return ret;
    }

    // Add New Entry to incoming queue
    list_add(&shared_inode_incoming->hifs_inode_list, &shared_inode_incoming_lst);

    return ret;
}

int read_from_block_dev(char *dev_file, int size)
{
    int ret;

    shared_block_incoming = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_block, 0);
    if (!shared_block_incoming) {
        perror("hi_comm: Error mapping shared memory");
        return -1;
    }
    ret = read(fd_block, &shared_block_incoming->block, size);
    if (ret >= 0) {
        //shared_block_incoming->block[ret] = '\0';  // null-terminate the string, read doesn't play well.
        shared_block_incoming->block_size = ret;
        shared_block_incoming->count = 1;
    } else {
        // Handle error
        perror("hi_comm: Error reading from inode device file");
        return ret;
    }

    // Add New Entry to incoming queue
    list_add(&shared_inode_incoming->hifs_inode_list, &shared_inode_incoming_lst);

    return ret;
}

int read_from_cmd_dev(char *dev_file, int size)
{
    int ret;

    shared_cmd_incoming = mmap(0, sizeof(shared_cmd_incoming), PROT_READ | PROT_WRITE, MAP_SHARED, fd_cmd, 0);
    if (!shared_cmd_incoming) {
        perror("hi_comm: Error mapping shared memory");
        return -1;
    }
    ret = read(fd_cmd, &shared_cmd_incoming, size);
    if (ret >= 0) {
        //buffer[ret] = '\0';  // null-terminate the string, read doesn't play well.
    } else {
        // Handle error
        perror("hi_comm: Error reading from command device file");
        return ret;
    }

    // Add New Entry to incoming queue.
    list_add(&shared_inode_incoming->hifs_inode_list, &shared_inode_incoming_lst);

    return ret;
}

int write_to_inode_dev(char *buffer, int size)
{
    int ret;

    shared_inode_outgoing = mmap(0, sizeof(shared_inode_outgoing), PROT_READ | PROT_WRITE, MAP_SHARED, fd_inode, 0);
    if (!shared_inode_outgoing) {
        perror("hi_comm: Error mapping shared memory");
        return -1;
    }
    ret = write(fd_inode, &buffer, size);

    // Unmap temp shared memory and remove outgoing item from list

    if (ret == -1) {
        perror("hi_comm: Error writing to device file");
        return -1;
    }
    return ret;

}

int write_to_block_dev(char *buffer, int size)
{
    int ret;

    shared_block_outgoing = mmap(0, sizeof(shared_block_outgoing), PROT_READ | PROT_WRITE, MAP_SHARED, fd_block, 0);
    if (!shared_block_outgoing) {
        perror("hi_comm: Error mapping shared memory");
        return -1;
    }
    ret = write(fd_block, &buffer, size);

    // Unmap temp shared memory and remove outgoing item from list

    if (ret == -1) {
        perror("hi_comm: Error writing to device file");
        return -1;
    }
    return ret;

}

int write_to_cmd_dev(char *buffer, int size)
{
    int ret;

    shared_cmd_outgoing = mmap(0, sizeof(shared_cmd_outgoing), PROT_READ | PROT_WRITE, MAP_SHARED, fd_cmd, 0);
    if (!shared_cmd_outgoing) {
        perror("hi_comm: Error mapping shared memory");
        return -1;
    }
    ret = write(fd_cmd, &buffer, size);

    // Unmap temp shared memory and remove outgoing item from list

    if (ret == -1) {
        perror("hi_comm: Error writing to device file");
        return -1;
    }
    return ret;

}