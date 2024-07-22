#include <stdio.h>
#include <stdlib.h>
#include <ext2fs/ext2_fs.h>
#include <ext2fs/ext2fs.h>

int main() {
    struct ext2_inode inode;
    printf("Size of ext2 inode in Bytes: %lu\n", sizeof(inode));
    printf("Size of ext2 inode struct in Bytes: %lu\n", sizeof(struct ext2_inode));
    return 0;
}