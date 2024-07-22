/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */
#pragma once

#include <getopt.h>
#include <stdbool.h>
#include <ext2fs/ext2_fs.h>
#include <ext2fs/ext2fs.h>


struct command_line_flags {
    char *target_drive;
    char *vlabel;
    char *device_name;
    char *mount_point;
    long long int size;
    long blocks;
    int block_size;
    int verbose;
    bool lethal_force;
    bool noaction;
    bool lazy_init;
} flags;
