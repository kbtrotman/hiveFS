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

int ret;
char *shared_block;
struct hifs_inode *shared_inode;
char *shared_cmd;
const char *atomic_device = ATOMIC_DEVICE_NAME;
char atomic_path[];
char atomic_device_name[256];  // Make sure this is large enough
char device_file_inode[256];
char device_file_block[256];
char device_file_cmd[50];


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
    int fd_inode = open(DEVICE_FILE_INODE, O_RDWR);
    if (fd_inode == -1) {
        perror("Error opening inode device file");
        return EXIT_FAILURE;
    }

    shared_inode = mmap(NULL, sizeof(struct hifs_inode), PROT_READ | PROT_WRITE, MAP_SHARED, fd_inode, 0);
    if (shared_inode == MAP_FAILED) {
        perror("Error mapping inode memory");
        close(fd_inode);
        return EXIT_FAILURE;
    }

    int fd_block = open(DEVICE_FILE_BLOCK, O_RDWR);
    if (fd_block == -1) {
        perror("Error opening block device file");
        munmap(shared_inode, sizeof(struct hifs_inode));
        close(fd_inode);
        return EXIT_FAILURE;
    }

    shared_block = (char *)mmap(NULL, HIFS_BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd_block, 0);

    if (shared_block == MAP_FAILED) {
        perror("Error mapping block memory");
        close(fd_block);
        munmap(shared_inode, sizeof(struct hifs_inode));
        close(fd_inode);
        return EXIT_FAILURE;
    }


    close(fd_inode);

    return EXIT_SUCCESS;
}