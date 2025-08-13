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

ssize_t hi_comm_inode_device_write(struct file *filep, const char  __user *buffer, size_t count, loff_t *offset) {
    // Write in user space to here in kernel
    ssize_t result = 0;
    hifs_info("In [write] for [INODE]\n");
    //New node incoming, so allocate it, copy data to it, and then put it on the queue and NULL the node.
    shared_inode_incoming = kmalloc(sizeof(*shared_inode_incoming), GFP_KERNEL);

    if (copy_from_user(shared_inode_incoming, buffer, min(sizeof(*shared_inode_incoming), count))) {
        // handle error
        return -EFAULT;
    }

    result = min(sizeof(*shared_inode_incoming), count);
    list_add(&shared_inode_incoming->hifs_inode_list, &shared_inode_incoming_lst);
    hifs_info("Added new Inode to the kernel incoming queue")
    //wake up the waitqueue
    wake_up(&waitqueue);
    shared_inode_incoming = NULL;
    hifs_poll_read = true;
    return result;
}

ssize_t hi_comm_block_device_write(struct file *filep, const char __user *buffer, size_t count, loff_t *offset) {
    struct hifs_blocks *kernel_data;
    struct hifs_blocks_user *user_data;

    kernel_data = kmalloc(sizeof(*kernel_data), GFP_KERNEL);
    if (!kernel_data) return -ENOMEM;

    if (copy_from_user(kernel_data, buffer, min(sizeof(*kernel_data), count))) {
        kfree(kernel_data);
        return -EFAULT;
    }

    user_data = hifs_parse_block_struct(kernel_data);
    if (!user_data) {
        kfree(kernel_data);
        return -ENOMEM;
    }

    void *buf = bpf_ringbuf_output(block_ringbuf, user_data, sizeof(*user_data), 0);
    if (!buf) {
        hifs_err("Failed to push block data to ringbuffer\n");
        kfree(user_data);
        return -ENOMEM;
    }

    kfree(kernel_data); // kernel_data is no longer needed
    return min(sizeof(*kernel_data), count);
}

ssize_t hi_comm_cmd_device_write(struct file *filep, const char __user *buffer, size_t count, loff_t *offset) {
    // Write in user space to here in kernel
    ssize_t result = 0;
    hifs_info("In [write] for [CMD]\n");
    //New node incoming, so allocate it, copy data to it, and then put it on the queue and NULL the node.
    shared_cmd_incoming = kmalloc(sizeof(*shared_cmd_incoming), GFP_KERNEL);

    if (copy_from_user(shared_cmd_incoming, buffer, min(sizeof(*shared_cmd_incoming), count))) {
        return -EFAULT;
    }

    result = min(sizeof(*shared_cmd_incoming), count);
    hifs_debug("Read command [%s] and count [%d] with size [%ld]", shared_cmd_incoming->cmd, shared_cmd_incoming->count, result);
    list_add(&shared_cmd_incoming->hifs_cmd_list, &shared_cmd_incoming_lst);
    hifs_info("Added new Command to the kernel incoming queue")
    //wake up the waitqueue
    wake_up(&waitqueue);
    shared_cmd_incoming = NULL;
    hifs_poll_read = true;
    return result;
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