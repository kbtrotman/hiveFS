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
struct hifs_link hifs_user_link;
struct hifs_link hifs_kern_link;

int ret, fd_cmd, fd_inode, fd_block;
char buffer[4096];




struct ring_buffer *inode_ringbuf;
struct ring_buffer *block_ringbuf;
struct ring_buffer *cmd_ringbuf;

int read_from_ringbuffer(int fd, struct ring_buffer *rb) {
    void *data;
    data = ring_buffer__reserve_space(rb, sizeof(struct hifs_inode_user), NULL);
    if (!data) {
        hifs_err("Failed to read from ringbuffer\n");
        return -1;
    }

    struct hifs_inode_user *inode_data = data;
    hifs_info("Received inode: %s, size: %d\n", inode_data->i_name, inode_data->i_size);
    return 0;
}

int write_to_ringbuffer(int fd, struct hifs_inode_user *data) {
    void *buf = ring_buffer__reserve_space(inode_ringbuf, sizeof(*data), NULL);
    if (!buf) {
        hifs_err("Failed to reserve ringbuffer space\n");
        return -1;
    }

    memcpy(buf, data, sizeof(*data));
    ring_buffer__submit(inode_ringbuf, NULL);
    hifs_info("Wrote inode data to ringbuffer\n");
    return 0;
}

char *hifs_strlcpy(char *dest_string, const char *src_string, int max_size)
{
    // User-Space implementation of strlcpy().
    if (strlen(src_string) < 1) {
        dest_string = NULL;
        return dest_string;
    }
    if (strlen(src_string) < max_size) {
        if ( (strlen(src_string) == 1) && (src_string[0] == '\0') ) { 
            dest_string = NULL; 
            return dest_string; 
        }
        max_size = strlen(src_string) + 1;
    }
    hifs_debug("hi_command: [strlcpy] Transferring string data between items [%s] with [%d] characters\n", src_string, max_size);
    strncpy(dest_string, src_string, max_size); // Copy at most max_size characters

    dest_string[max_size - 1] = '\0'; // Forcibly Null-terminate the destination string
    return dest_string;
}