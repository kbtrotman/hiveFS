/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"

extern struct hifs_link hifs_kern_link;
extern struct hifs_link hifs_user_link;

extern const char *kern_atomic_device;
extern const char *user_atomic_device;
extern char kern_atomic_device_name[256];
extern char user_atomic_device_name[256];
extern char kern_atomic_path[20];
extern char user_atomic_path[20];
extern int kern_atomic_value;
extern int user_atomic_value;

extern char *device_file_inode;
extern char *device_file_block;
extern char *device_file_cmd;
extern int fd_cmd, fd_inode, fd_block;

extern char buffer[4096];
extern struct PSQL sqldb;

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

struct pollfd *cmd_pfd;
struct pollfd *inode_pfd;
struct pollfd *block_pfd;


int main(int argc, char *argv[])
{
    int ret;
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

    strcpy(kern_atomic_path, "/dev/");
    strcpy(kern_atomic_device_name, kern_atomic_path);
    strcat(kern_atomic_device_name, kern_atomic_device);

    strcpy(user_atomic_path, "/dev/");
    strcpy(user_atomic_device_name, user_atomic_path);
    strcat(user_atomic_device_name, user_atomic_device);

    strcpy(device_file_inode, user_atomic_path);
    strcat(device_file_inode, DEVICE_FILE_INODE);

    strcpy(device_file_block, user_atomic_path);
    strcat(device_file_block, DEVICE_FILE_BLOCK);

    strcpy(device_file_cmd, user_atomic_path);
    strcat(device_file_cmd, DEVICE_FILE_CMDS);

    init_hive_link();

    cmd_pfd = malloc(sizeof(struct pollfd));
    inode_pfd = malloc(sizeof(struct pollfd));
    block_pfd = malloc(sizeof(struct pollfd));

    fd_cmd = open(device_file_cmd, O_RDWR | O_NONBLOCK);   
    if( fd_cmd == -1 )  { perror("hi-command: open [CMD queue]\n"); exit(EXIT_FAILURE); }

    fd_inode = open(device_file_cmd, O_RDWR | O_NONBLOCK);   
    if( fd_inode == -1 )  { perror("hi-command: open [INODE queue]\n"); exit(EXIT_FAILURE); }

    fd_block = open(device_file_cmd, O_RDWR | O_NONBLOCK);   
    if( fd_block == -1 )  { perror("hi-command: open [BLOCK queue]\n"); exit(EXIT_FAILURE); }

    hifs_init_queues();

    cmd_pfd->fd = fd_cmd;
    cmd_pfd->events = ( POLLIN | POLLOUT );

    inode_pfd->fd = fd_inode;
    inode_pfd->events = POLLIN | POLLOUT;

    block_pfd->fd = fd_block;
    block_pfd->events = POLLIN | POLLOUT;

    hifs_comm_set_program_up(HIFS_COMM_PROGRAM_USER_HICOMM);
    hifs_comm_set_program_up(HIFS_COMM_PROGRAM_KERN_MOD);

while (1) {
    kern_atomic_value = hifs_comm_check_program_up(HIFS_COMM_PROGRAM_KERN_MOD);
    if (kern_atomic_value == HIFS_COMM_LINK_DOWN) {
        printf("hi-command: Kernel link is down. Waiting for kernel module to come up...\n");
        continue;
    } else if (hifs_kern_link.state == HIFS_COMM_LINK_DOWN) {
        printf("hi-command: Kernel link was recently up'd. Proceeding...\n");
        hifs_comm_set_program_up(HIFS_COMM_PROGRAM_KERN_MOD);
    }

    printf("hi-command: Looping polls...\n");
    ret = poll(cmd_pfd, (unsigned long)1, 3000);   //wait for 5secs
    
    if( ret < 0 ) 
    {
        perror("hi-command: poll\n");
        assert(0);
    }
    
    //User-space has total access to this file, so the order here determines which direction is processed first.
    if( ( cmd_pfd->revents & POLLIN )  == POLLIN )
    {   
        read_from_queue();
        printf("hi-command: POLLOUT\n");
    }

    write_to_queue();
    printf("hi-command: POLLIN\n");
    
    printf("hi-command: kernel module status is: %d\n", hifs_kern_link.state);
    printf("hi-command: user-space status is: %d\n", hifs_user_link.state);
}
    
}