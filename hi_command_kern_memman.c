/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#include "hifs_shared_defs.h"
#include "hifs.h"


// Globals
extern struct hifs_link hifs_kern_link;
extern struct hifs_link hifs_user_link;

extern struct bpf_map *inode_ringbuf_k2u;
extern struct bpf_map *block_ringbuf_k2u;
extern struct bpf_map *cmd_ringbuf_k2u;

extern struct bpf_map *inode_ringbuf_u2k;
extern struct bpf_map *block_ringbuf_u2k;
extern struct bpf_map *cmd_ringbuf_u2k;

// These are for the 3 devices which controls 6 queues (1 each way) for transferring 3 types of data: inode, block, & cmd.
int register_all_comm_ringbuffers(void) {
    // Create ringbuffers for outgoing data (kernel -> user)
    inode_ringbuf = bpf_map_create(BPF_MAP_TYPE_RINGBUF, "hifs_inode_ringbuf", 0, sizeof(struct hifs_inode_user), RINGBUF_SIZE, NULL);
    if (!inode_ringbuf) {
        hifs_err("Failed to create inode ringbuffer\n");
        return -ENOMEM;
    }

    block_ringbuf = bpf_map_create(BPF_MAP_TYPE_RINGBUF, "hifs_block_ringbuf", 0, sizeof(struct hifs_blocks_user), RINGBUF_SIZE, NULL);
    if (!block_ringbuf) {
        hifs_err("Failed to create block ringbuffer\n");
        bpf_map_delete(inode_ringbuf);
        return -ENOMEM;
    }

    cmd_ringbuf = bpf_map_create(BPF_MAP_TYPE_RINGBUF, "hifs_cmd_ringbuf", 0, sizeof(struct hifs_cmds_user), RINGBUF_SIZE, NULL);
    if (!cmd_ringbuf) {
        hifs_err("Failed to create command ringbuffer\n");
        bpf_map_delete(inode_ringbuf);
        bpf_map_delete(block_ringbuf);
        return -ENOMEM;
    }

    hifs_info("eBPF ringbuffers initialized\n");
    return 0;
}

void unregister_all_comm_ringbuffers(void) {
    if (inode_ringbuf) bpf_map_delete(inode_ringbuf);
    if (block_ringbuf) bpf_map_delete(block_ringbuf);
    if (cmd_ringbuf) bpf_map_delete(cmd_ringbuf);
    hifs_info("eBPF ringbuffers destroyed\n");
}

ssize_t hi_comm_inode_device_write(struct file *filep, const char __user *buffer, size_t count, loff_t *offset) {
    struct hifs_inode *inode = kmalloc(sizeof(*inode), GFP_KERNEL);
    if (!inode) return -ENOMEM;

    if (copy_from_user(inode, buffer, min(sizeof(*inode), count))) {
        kfree(inode);
        return -EFAULT;
    }

    void *buf = bpf_ringbuf_output(inode_ringbuf_k2u, inode, sizeof(*inode), 0);
    if (!buf) {
        hifs_err("Failed to push inode data to k2u ring buffer");
        kfree(inode);
        return -ENOMEM;
    }

    kfree(inode);
    return min(sizeof(*inode), count);
}



ssize_t hi_comm_block_device_write(struct file *filep, const char __user *buffer, size_t count, loff_t *offset) {
    struct hifs_blocks *kernel_data = kmalloc(sizeof(*kernel_data), GFP_KERNEL);
    if (!kernel_data) return -ENOMEM;

    if (copy_from_user(kernel_data, buffer, min(sizeof(*kernel_data), count))) {
        kfree(kernel_data);
        return -EFAULT;
    }

    struct hifs_blocks_user *user_data = hifs_parse_block_struct(kernel_data);
    if (!user_data) {
        kfree(kernel_data);
        return -ENOMEM;
    }

    void *buf = bpf_ringbuf_output(block_ringbuf_k2u, user_data, sizeof(*user_data), 0);
    if (!buf) {
        hifs_err("Failed to push block data to k2u ring buffer");
        kfree(user_data);
        kfree(kernel_data);
        return -ENOMEM;
    }

    kfree(kernel_data);
    return min(sizeof(*kernel_data), count);
}


ssize_t hi_comm_cmd_device_write(struct file *filep, const char __user *buffer, size_t count, loff_t *offset) {
    struct hifs_cmd *cmd = kmalloc(sizeof(*cmd), GFP_KERNEL);
    if (!cmd) return -ENOMEM;

    if (copy_from_user(cmd, buffer, min(sizeof(*cmd), count))) {
        kfree(cmd);
        return -EFAULT;
    }

    void *buf = bpf_ringbuf_output(cmd_ringbuf_k2u, cmd, sizeof(*cmd), 0);
    if (!buf) {
        hifs_err("Failed to push command data to k2u ring buffer");
        kfree(cmd);
        return -ENOMEM;
    }

    kfree(cmd);
    return min(sizeof(*cmd), count);
}



void write_blocks_bitmap(int fd, hifs_block_bitmap *bitmap, uint64_t bitmap_start) {
    uint64_t size_in_bytes = (bitmap->size + 7) / 8;

    if (pwrite(fd, bitmap->bitmap, size_in_bytes, bitmap_start * bitmap->blocksize) != size_in_bytes) {
        hifs_error("Error writing inode bitmap to disk");
    }
}

void toggle_inode_bitmap(ihifs_block_bitmap *bitmap, uint64_t block_number) {
    if (block_number >= bitmap->size) {
        hifs_error("Error: inode number out of range\n");
        return;
    }

    uint64_t byte_index = block_number / 8;
    uint8_t bit_index = block_number % 8;

    bitmap->bitmap[byte_index] ^= (1 << bit_index);
}

hifs_block_bitmap *read_inode_bitmap(int fd, uint64_t bitmap_start, uint64_t size_in_bits, uint64_t blocksize) {
    hifs_block_bitmap *bitmap = (hifs_block_bitmap *)malloc(sizeof(hifs_block_bitmap));
    if (!bitmap) {
        hifs_error("Unable to allocate memory for block bitmap");
        return NULL;
    }

    uint64_t size_in_bytes = (size_in_bits + 7) / 8; // Convert bits to bytes, rounding up
    bitmap->bitmap = (uint8_t *)malloc(size_in_bytes);
    if (!bitmap->bitmap) {
        hifs_error("Unable to allocate memory for bitmap data");
        free(bitmap);
        return NULL;
    }

    if (pread(fd, bitmap->bitmap, size_in_bytes, bitmap_start * blocksize) != size_in_bytes) {
        perror("Error reading inode bitmap from disk");
        free(bitmap->bitmap);
        free(bitmap);
        return NULL;
    }

    bitmap->size = size_in_bits;
    bitmap->blocksize = blocksize;
    return bitmap;
}
