/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <libpq-fe.h>
#include <netlink/socket.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <linux/atomic.h>
#include <linux/wait.h>


#include "hi_command.h"

int main(int argc, char *argv[])
{
    int fd_inode = open(DEVICE_FILE_INODE, O_RDWR);
    if (fd_inode == -1) {
        perror("Error opening inode device file");
        return EXIT_FAILURE;
    }

    struct inode *shared_inode = mmap(NULL, sizeof(struct inode), PROT_READ | PROT_WRITE, MAP_SHARED, fd_inode, 0);
    if (shared_inode == MAP_FAILED) {
        perror("Error mapping inode memory");
        close(fd_inode);
        return EXIT_FAILURE;
    }

    int fd_block = open(DEVICE_FILE_BLOCK, O_RDWR);
    if (fd_block == -1) {
        perror("Error opening block device file");
        munmap(shared_inode, sizeof(struct inode));
        close(fd_inode);
        return EXIT_FAILURE;
    }

    struct buffer_head *shared_block = mmap(NULL, sizeof(struct buffer_head), PROT_READ | PROT_WRITE, MAP_SHARED, fd_block, 0);
    if (shared_block == MAP_FAILED) {
        perror("Error mapping block memory");
        close(fd_block);
        munmap(shared_inode, sizeof(struct inode));
        close(fd_inode);
        return EXIT_FAILURE;
    }

    // Access and modify the shared_inode and shared_block as needed
    // Remember to use proper synchronization mechanisms for concurrent access

    // Unmap the memory
    if (munmap(shared_block, sizeof(struct buffer_head)) == -1) {
        perror("Error unmapping block memory");
    }

    close(fd_block);

    if (munmap(shared_inode, sizeof(struct inode)) == -1) {
        perror("Error unmapping inode memory");
    }

    close(fd_inode);

    return EXIT_SUCCESS;
}