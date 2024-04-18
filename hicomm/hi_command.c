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
extern const char *atomic_device;
extern char atomic_path[20];
extern char atomic_device_name[256]; 
extern char *device_file_inode;
extern char *device_file_block;
extern char *device_file_cmd;
extern int fd_cmd, fd_inode, fd_block;

extern char buffer[4096];
extern struct PSQL sqldb;
extern int atomic_value;
extern struct hifs_link hifs_user_link;

extern struct hifs_inode *shared_inode_outgoing;    // These six Doubly-Linked Lists are our
extern struct hifs_blocks *shared_block_outgoing;   // processing queues. They are sent & 
extern struct hifs_cmds *shared_cmd_outgoing;       // received thru the 3 device files known
extern struct hifs_inode *shared_inode_incoming;    // as the "queues" (to hi_command). We want
extern struct hifs_blocks *shared_block_incoming;   // to proces them fast, so they're split into
extern struct hifs_cmds *shared_cmd_incoming;       // incoming & outgoing queues here.

extern struct list_head shared_inode_outgoing_lst;    
extern struct list_head shared_block_outgoing_lst;    
extern struct list_head shared_cmd_outgoing_lst;       
extern struct list_head shared_inode_incoming_lst;    
extern struct list_head shared_block_incoming_lst;   
extern struct list_head shared_cmd_incoming_lst;  


int main(int argc, char *argv[])
{
    int ret;
    struct pollfd pfd;
    //bool queue_empty = true; // Add a flag to track whether the queue is empty

    hifs_user_link.clockstart = GET_TIME();
    sqldb.hive_conn = NULL;
    sqldb.last_qury = NULL;
    sqldb.last_ins = NULL;
    sqldb.col = 0;
    sqldb.row = 0;
    sqldb.rec_count = 0;
    sqldb.sql_init = false;

    device_file_inode = malloc(256);
    device_file_block = malloc(256);
    device_file_cmd = malloc(50);

    strcpy(atomic_path, "/dev/");
    strcpy(atomic_device_name, atomic_path);
    strcat(atomic_device_name, atomic_device);

    strcpy(device_file_inode, atomic_path);
    strcat(device_file_inode, DEVICE_FILE_INODE);

    strcpy(device_file_block, atomic_path);
    strcat(device_file_block, DEVICE_FILE_BLOCK);

    strcpy(device_file_cmd, atomic_path);
    strcat(device_file_cmd, DEVICE_FILE_CMDS);

    init_hive_link();

    fd_cmd = open(device_file_cmd, O_RDWR | O_NONBLOCK);   
    if( fd_cmd == -1 )  { perror("hi-command: open [CMD queue]"); exit(EXIT_FAILURE); }

    fd_inode = open(device_file_cmd, O_RDWR | O_NONBLOCK);   
    if( fd_inode == -1 )  { perror("hi-command: open [INODE queue]"); exit(EXIT_FAILURE); }

    fd_block = open(device_file_cmd, O_RDWR | O_NONBLOCK);   
    if( fd_block == -1 )  { perror("hi-command: open [BLOCK queue]"); exit(EXIT_FAILURE); }

    hifs_init_queues();

    pfd.fd = fd_cmd;
    pfd.events = ( POLLIN | POLLOUT );


queue_management:

    atomic_value = read_from_atomic();
    printf("hi-command: Atomic value: %d\n", atomic_value);
    
    // Here we ignore values 1,3,4,8,10
    if (atomic_value == HIFS_Q_PROTO_ACK_LINK_KERN || 
        atomic_value == HIFS_Q_PROTO_ACK_LINK_USER ||
        hifs_user_link.state == HIFS_COMM_LINK_DOWN)
    {
        // Call to complete a new or a temp aborted link_up here
        printf("hi_command: Waiting on link up. Re-trying...\n");
        hifs_comm_link_init_change();
    } else {
        // Queue contents manage themselves, so do nothing here....
    }

    puts("hi-command: Looping polls...");
    ret = poll(&pfd, (unsigned long)1, 5000);   //wait for 5secs
    
    if( ret < 0 ) 
    {
        perror("hi-command: poll");
        assert(0);
    }
    
    //User-space has total access to this file, so the order here determines which direction is processed first.
    if( ( pfd.revents & POLLIN )  == POLLIN )
    {
        read_from_queue();
        printf("hi-command: POLLIN");
    }
    
    if( ( pfd.revents & POLLOUT )  == POLLOUT )
    {
        write_to_queue();
        printf("hi-command: POLLOUT\n");
    }

    goto queue_management;
    
}