/**
 * HiveFS cache inspector
 */

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "tools.h"

static void print_usage(const char *prog)
{
    fprintf(stderr, "Usage: %s [--device /dev/hivefs_ctl]\n", prog);
}

int main(int argc, char **argv)
{
    const char *device = "/dev/" HIFS_COMM_DEVICE_NAME;
    int fd = -1;
    int opt;

    static struct option opts[] = {
        { "device", required_argument, NULL, 'd' },
        { "help",   no_argument,       NULL, 'h' },
        { NULL,      0,                 NULL,  0  }
    };

    while ((opt = getopt_long(argc, argv, "d:h", opts, NULL)) != -1) {
        switch (opt) {
        case 'd':
            device = optarg;
            break;
        case 'h':
            print_usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            print_usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    fd = open(device, O_RDONLY);
    if (fd < 0) {
        perror("open hivefs control device");
        return EXIT_FAILURE;
    }

    struct hifs_cache_status stats;
    memset(&stats, 0, sizeof(stats));

    if (ioctl(fd, HIFS_IOCTL_CACHE_STATUS, &stats) < 0) {
        perror("ioctl HIFS_IOCTL_CACHE_STATUS");
        close(fd);
        return EXIT_FAILURE;
    }

    close(fd);

    double block_pct = 0.0;
    if (stats.block_bytes_total)
        block_pct = (double)stats.block_bytes_used / (double)stats.block_bytes_total * 100.0;

    double dirent_pct = 0.0;
    if (stats.dirent_bytes_total)
        dirent_pct = (double)stats.dirent_bytes_used / (double)stats.dirent_bytes_total * 100.0;

    double inode_pct = 0.0;
    if (stats.inode_bytes_total)
        inode_pct = (double)stats.inode_bytes_used / (double)stats.inode_bytes_total * 100.0;

    printf("HiveFS cache statistics:\n");
    printf("  Block cache : %10llu / %10llu bytes (%5.1f%% used), unit %u, LRU %u/%u\n",
           (unsigned long long)stats.block_bytes_used,
           (unsigned long long)stats.block_bytes_total,
           block_pct,
           stats.block_unit_bytes,
           stats.block_fifo_count,
           stats.block_fifo_capacity);

    printf("  Dirent cache: %10llu / %10llu bytes (%5.1f%% used), unit %u, LRU %u/%u\n",
           (unsigned long long)stats.dirent_bytes_used,
           (unsigned long long)stats.dirent_bytes_total,
           dirent_pct,
           stats.dirent_unit_bytes,
           stats.dirent_fifo_count,
           stats.dirent_fifo_capacity);

    printf("  Inode cache : %10llu / %10llu bytes (%5.1f%% used), unit %u, LRU %u/%u\n",
           (unsigned long long)stats.inode_bytes_used,
           (unsigned long long)stats.inode_bytes_total,
           inode_pct,
           stats.inode_unit_bytes,
           stats.inode_fifo_count,
           stats.inode_fifo_capacity);

    return EXIT_SUCCESS;
}
