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
extern struct hifs_blocks *shared_block_outgoing;   // processing queues.
extern struct hifs_cmds *shared_cmd_outgoing;       
extern struct hifs_inode *shared_inode_incoming;   
extern struct hifs_blocks *shared_block_incoming;   
extern struct hifs_cmds *shared_cmd_incoming;      

extern struct list_head shared_inode_outgoing_lst;    
extern struct list_head shared_block_outgoing_lst;    
extern struct list_head shared_cmd_outgoing_lst;       
extern struct list_head shared_inode_incoming_lst;    
extern struct list_head shared_block_incoming_lst;   
extern struct list_head shared_cmd_incoming_lst;  

struct pollfd *cmd_pfd;
struct pollfd *inode_pfd;
struct pollfd *block_pfd;

const char Tab_Names[TAB_COUNT][TAB_NAME_LENGTH] = {
    "Hi_Command Log    ",
    "Queue Status      ",
    "I/O Perf          ",
    "Cache Items       "
};

WINDOW *tab_content[TAB_COUNT];
WINDOW *tab_headers[TAB_COUNT];
PANEL *tab_panels[TAB_COUNT];
int log_line = 1;
int current_tab = 0;

int main(int argc, char *argv[])
{
    int ret;
    //int ch;

    hifs_user_link.clockstart = GET_TIME();
    sqldb.hive_conn = NULL;
    sqldb.last_qury = NULL;
    sqldb.last_ins = NULL;
    sqldb.cols = 0;
    sqldb.rows = 0;
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

    cmd_pfd = malloc(sizeof(struct pollfd));
    inode_pfd = malloc(sizeof(struct pollfd));
    block_pfd = malloc(sizeof(struct pollfd));

/*
    initscr();
    cbreak();
    noecho();
    //keypad(stdscr, TRUE);
    hicomm_draw_new_Content(current_tab);
    hicomm_draw_tabs(current_tab);
*/
    fd_cmd = open(device_file_cmd, O_RDWR | O_NONBLOCK);   
    if( fd_cmd == -1 )  { 
        hi_comm_safe_cleanup();
        printf("Please start the HiveFS kernel module first before starting Hi_Command\n");
        hifs_err("hi-command: error in open [CMD queue]\n"); exit(EXIT_FAILURE); 
    }

    fd_inode = open(device_file_cmd, O_RDWR | O_NONBLOCK);   
    if( fd_inode == -1 )  { 
        hi_comm_safe_cleanup();
        printf("Please start the HiveFS kernel module first before starting Hi_Command\n");
        hifs_err("hi-command: error in open [INODE queue]\n"); exit(EXIT_FAILURE); 
    }

    fd_block = open(device_file_cmd, O_RDWR | O_NONBLOCK);   
    if( fd_block == -1 )  { 
        hi_comm_safe_cleanup();
        printf("Please start the HiveFS kernel module first before starting Hi_Command\n");
        hifs_err("hi-command: error in open [BLOCK queue]\n"); exit(EXIT_FAILURE); 
    }

    hifs_init_queues();

    cmd_pfd->fd = fd_cmd;
    cmd_pfd->events = ( POLLIN | POLLOUT );

    inode_pfd->fd = fd_inode;
    inode_pfd->events = POLLIN | POLLOUT;

    block_pfd->fd = fd_block;
    block_pfd->events = POLLIN | POLLOUT;

    hifs_comm_set_program_up(HIFS_COMM_PROGRAM_USER_HICOMM);
    hifs_comm_set_program_up(HIFS_COMM_PROGRAM_KERN_MOD);

    init_hive_link();

    ret = register_hive_host();
    if (ret == 0) {
        hifs_crit("hi-command: error while registering host with hive, bailing out\n");
        hi_comm_safe_cleanup();
        exit(EXIT_FAILURE);
    }

    //while ((ch = getch()) != 'q') {
    while (1) {
        kern_atomic_value = hifs_comm_check_program_up(HIFS_COMM_PROGRAM_KERN_MOD);
        if (kern_atomic_value == HIFS_COMM_LINK_DOWN) {
            hifs_info("hi-command: Kernel link is down. Waiting for kernel module to come up...\n");
            continue;
        } else if (hifs_kern_link.state == HIFS_COMM_LINK_DOWN) {
            hifs_info("hi-command: Kernel link was recently up'd. Proceeding...\n");
            hifs_comm_set_program_up(HIFS_COMM_PROGRAM_KERN_MOD);
        }

        hifs_info("hi-command: Looping polls...\n");
        ret = poll(cmd_pfd, (unsigned long)1, 300);   //wait for 3 secs
        
        if( ret < 0 ) 
        {
            hifs_info("hi-command: poll\n");
            assert(0);
        }
        
        //User-space has total access to this file, so the order here determines which direction is processed first.
        if( ( cmd_pfd->revents & POLLIN )  == POLLIN )
        {   
            read_from_queue();
            hifs_info("hi-command: POLLOUT (Read from user side finished\n");
        }

        write_to_queue();
        hifs_info("hi-command: POLLIN (Write from user side finished)\n");
        
        hifs_debug("hi-command: kernel module status is: %d\n", hifs_kern_link.state);
        hifs_debug("hi-command: user-space status is: %d\n", hifs_user_link.state);
/*
        switch (ch) {
            case 'q':
                // Clean up
                for (int i = 0; i < TAB_COUNT; i++) {
                    del_panel(tab_panels[i]);
                    delwin(tab_content[i]);
                    delwin(tab_headers[i]);
                }
                endwin();
                exit(0);
            case KEY_LEFT:
                if (current_tab > 0) current_tab--;
                break;
            case KEY_RIGHT:
                if (current_tab < TAB_COUNT - 1) current_tab++;
                break;
            case KEY_BTAB:
                if (current_tab < 3) current_tab++;
                if (current_tab == 3) current_tab = 0;
                break;
            case '1':
                current_tab = 0;
                break;
            case '2':
                current_tab = 1;
                break;
            case '3':
                current_tab = 2;
                break;
            case '4':
                current_tab = 3;
                break;
            default:
                break;
        }
        switch_tab(current_tab);
        hicomm_draw_tabs(current_tab);
        wrefresh(tab_headers[current_tab]);
*/
    }
/*
   // Clean up
    for (int i = 0; i < TAB_COUNT; i++) {
        del_panel(tab_panels[i]);
        delwin(tab_content[i]);
        delwin(tab_headers[i]);
    }
    endwin();
*/  

    hi_comm_safe_cleanup();
    return 0;
}