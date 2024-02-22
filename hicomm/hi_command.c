/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"

struct hifs_link hifs_user_link;


int main(int argc, char *argv[])
{
    hifs_user_link.clockstart = GET_TIME();
    int atomic_value;
    hi_comm_queue_init(); 
    atomic_value = read_from_atomic();
    printf("Atomic value: %d\n", atomic_value);
    if (atomic_value == 0) {

        printf("Atomic value: %d\n", atomic_value);
    }
    else if (atomic_value == 9) {
        printf("hi-command: Received hivefs Link_Up Command to user-space.\n");
        hifs_user_link.last_state = hifs_user_link.state;
        hifs_user_link.state = HIFS_COMM_LINK_UP;
        printf("hi-command: user link up'd at %ld seconds after hi_command start.\n", (GET_TIME() - hifs_user_link.clockstart));
        hifs_user_link.last_check = 0;
        write_to_atomic(8);
        printf("Atomic value: %d\n", atomic_value);
    }
    else {
        printf("Atomic value: %d\n", atomic_value);
    }
}