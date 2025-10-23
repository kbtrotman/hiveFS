#include <stdio.h>
#include <stdlib.h>
#include "tools.h"

int main() {
    struct hifs_inode inode;
    printf("Size of hifs inode in Bytes: %lu\n", sizeof(inode));
    printf("Size of hifs inode struct in Bytes: %lu\n", sizeof(struct hifs_inode));
    return 0;
}