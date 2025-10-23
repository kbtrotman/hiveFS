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
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "../hifs_shared_defs.h"

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
