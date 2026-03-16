/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

 #include <string.h>

#include "hive_guard_mcl.h"

int hifs_mcl_open(...);
int hifs_mcl_close(...);
int hifs_mcl_append(...);
int hifs_mcl_replay(...);
int hifs_mcl_checkpoint(...);
int hifs_mcl_mark_placement_assigned(...);
int hifs_mcl_mark_fragment_sent(...);
int hifs_mcl_mark_committed(...);