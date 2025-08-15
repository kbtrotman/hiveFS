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

extern struct bpf_map *cmd_ringbuf_k2u;
extern struct bpf_map *cmd_ringbuf_u2k;

// These are for devices which control queues (1 each way) for transferring 3 types of data: inode, block, & cmd via memory.
int hic_register_ringbuffers(void) {
    // Create ringbuffers for outgoing data (kernel -> user) and incoming data (user -> kernel)
    cmd_ringbuf_k2u = bpf_map_get("/sys/fs/bpf/hic_ringbuff_k2u");
    cmd_ringbuf_u2k = bpf_map_get("/sys/fs/bpf/hic_ringbuff_u2k");

    if (!cmd_ringbuf_k2u || !cmd_ringbuf_u2k) {
        hifs_err("fs_kmod: Failed to get one of the bi-directional ring buffers.\n");
        return -EINVAL;
    }
    return 0;
}

void hic_unregister_ringbuffers(void) {
    if (cmd_ringbuf_k2u) bpf_map_delete(cmd_ringbuf_k2u);
    if (cmd_ringbuf_u2k) bpf_map_delete(cmd_ringbuf_u2k);
    hifs_info("eBPF ringbuffers destroyed for shutdown\n");
}

ssize_t hic_ringbuffer_write(struct hic_ringbuffer buffer, size_t count) {

    void *buf = bpf_ringbuf_output(cmd_ringbuf_k2u, buffer, sizeof(*buffer), 0);
    kfree(buffer);
    if (!buf) {
        hifs_err("Failed to push inode data to k2u ring buffer"); 
        return -ENOMEM;
    } else {
        pr_info("fs_kmod: Sent command: %s\n", payload);
    }
    return min(sizeof(*buffer), count);
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
