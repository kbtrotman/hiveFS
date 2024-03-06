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
char atomic_path[];
char atomic_device_name[256];  // Make sure this is large enough
char device_file_inode[256];
char device_file_block[256];
char device_file_cmd[50];

struct hifs_inode *shared_inode_outgoing;    // These six Doubly-Linked Lists are our
struct hifs_blocks *shared_block_outgoing;   // processing queues. They are sent & 
struct hifs_cmds *shared_cmd_outgoing;       // received thru the 3 device files known
struct hifs_inode *shared_inode_incoming;    // as the "queues" (to hi_command). We want
struct hifs_blocks *shared_block_incoming;   // to proces them fast, so they're split into
struct hifs_cmds *shared_cmd_incoming;       // incoming & outgoing queues here.


int read_from_atomic(void)
{
    int fd;
    int value;

    fd = open(atomic_device_name, O_RDWR);
    if (fd == -1) {
        perror("Error opening device file");
        return -1;
    }

    // Reading from the atomic variable in the kernel space
    read(fd, &value, sizeof(int));
    printf("Read value from kernel: %d\n", value);
    close(fd);
    return value;
}

int write_to_atomic(int value)
{
    int fd;

    fd = open( atomic_device_name, O_RDWR);
    if (fd == -1) {
        perror("Error opening device file");
        return -1;
    }

    // Writing to the atomic variable in the kernel space
    write(fd, &value, sizeof(int));
    printf("Wrote value to kernel: %d\n", value);

    close(fd);
    return value;
}

int hi_comm_queue_init(void)
{





    return EXIT_SUCCESS;
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

int read_from_dev(char dev_file, int size)
{
    int ret;

    if (strcmp(&dev_file, device_file_inode) == 0) {
        int fd_inode = open(device_file_inode, O_RDWR);
        if (fd_inode == -1) {
            perror("Error opening inode device file");
            return EXIT_FAILURE;
        }
        shared_inode_incoming = mmap(0, sizeof(shared_inode_incoming), PROT_READ | PROT_WRITE, MAP_SHARED, fd_inode, 0);
        if (!shared_inode_incoming) {
            perror("Error mapping shared memory");
            return -1;
        }
        ret = read(fd_inode, &buffer, size);

        // Add New Entry to incoming queue

        close(fd_inode);
    } else if (strcmp(&dev_file, device_file_block) == 0) {
        int fd_block = open(device_file_block, O_RDWR);
        if (fd_block == -1) {
            perror("Error opening block device file");
            return EXIT_FAILURE;
        }
        shared_block_incoming = mmap(0, sizeof(shared_block_incoming), PROT_READ | PROT_WRITE, MAP_SHARED, fd_block, 0);
        if (!shared_block_incoming) {
            perror("Error mapping shared memory");
            return -2;
        }
        ret = read(fd_block, &buffer, size);
        
        // Add New Entry to incoming queue

        close(fd_block);
    } else if (strcmp(&dev_file, device_file_cmd) == 0) {   
        int fd_cmd = open(device_file_cmd, O_RDWR);
        if (fd_cmd == -1) {
            perror("Error opening command device file");
            return EXIT_FAILURE;
        }
        shared_cmd_incoming = mmap(0, sizeof(shared_cmd_incoming), PROT_READ | PROT_WRITE, MAP_SHARED, fd_cmd, 0);
        if (!shared_cmd_incoming) {
            perror("Error mapping shared memory");
            return -3;
        }
        ret = read(fd_cmd, &buffer, size);

        // Add New Entry to incoming queue

        close(fd_cmd);
    } else {
        return -4;
    }
    
    if (ret == -1) {
        perror("Error reading from device file");
        return ret;
    } else {
        return ret;
    }

}

int write_to_dev(char *buffer, int size, char dev_file)
{
    int ret;

    if (strcmp(&dev_file, device_file_inode) == 0) {
        shared_inode_outgoing = mmap(0, sizeof(shared_inode_outgoing), PROT_READ | PROT_WRITE, MAP_SHARED, fd_inode, 0);
        if (!shared_inode_outgoing) {
            perror("Error mapping shared memory");
            return -1;
        }
        ret = write(fd_inode, &buffer, size);

        // Unmap temp shared memory and remove outgoing item from list

        close(fd_inode);
    } else if (strcmp(&dev_file, device_file_block) == 0) {
        shared_block_outgoing = mmap(0, sizeof(shared_block_outgoing), PROT_READ | PROT_WRITE, MAP_SHARED, fd_block, 0);
        if (!shared_block_outgoing) {
            perror("Error mapping shared memory");
            return -2;
        }
        ret = write(fd_block, &buffer, size);

         // Unmap temp shared memory and remove outgoing item from list

        close(fd_block);
    } else if (strcmp(&dev_file, device_file_cmd) == 0) {
        shared_cmd_outgoing = mmap(0, sizeof(shared_cmd_outgoing), PROT_READ | PROT_WRITE, MAP_SHARED, fd_cmd, 0);
        if (!shared_cmd_outgoing) {
            perror("Error mapping shared memory");
            return -3;
        }
        ret = write(fd_cmd, &buffer, size);

        // Unmap temp shared memory and remove outgoing item from list

        close(fd_cmd);
    } else {
        return -4;
    }
    if (ret == -1) {
        perror("Error writing to device file");
        return -1;
    }
    return ret;

}