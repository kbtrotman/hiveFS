/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"

extern const char Tab_Names[TAB_COUNT][TAB_NAME_LENGTH];
extern WINDOW *tab_content[TAB_COUNT];
extern WINDOW *tab_headers[TAB_COUNT];
extern PANEL *tab_panels[TAB_COUNT];
extern int log_line;

void draw_tab_headers(int current_tab) 
{
    for (int i = 0; i < TAB_COUNT; i++) {
        tab_headers[i] = newwin(1, TAB_WIDTH, i * TAB_HEIGHT, 0);
        if (i == current_tab) {
            wattron(tab_headers[i], A_REVERSE);
        }
        mvwprintw(tab_headers[i], 0, 0, Tab_Names[i]);
        if (i == current_tab) {
            wattroff(tab_headers[i], A_REVERSE);
        }
        //wprintw(tab_headers, "%s", Tab_Names[i]);
    }
    wrefresh(tab_headers[current_tab]);
}

void switch_tab(int tab_index) 
{
    for (int i = 0; i < TAB_COUNT; i++) {
        hide_panel(tab_panels[i]);
    }
    show_panel(tab_panels[tab_index]);
    update_panels();
    doupdate();
}

void hicomm_draw_tab_contents(int tab_index) 
{
    // Initialize tab headers window
    draw_tab_headers(tab_index);

    // Initialize tab content windows and panels
    for (int i = 0; i < TAB_COUNT; i++) {
        tab_content[i] = newwin(TAB_CONTENT_HEIGHT, TAB_CONTENT_WIDTH, TAB_HEADER_HEIGHT + 1, 0);
        tab_panels[i] = new_panel(tab_content[i]);
    }
}

long hifs_get_host_id( void ) {
    long host_id;
    host_id = gethostid();

    if (host_id) {
        return host_id;
    }
    return 0;
}

char *hifs_get_machine_id( void ) {

    char *machine_id;
    char filename[50] = "/etc/machine-id";
    
    machine_id = hifs_read_file_to_string(filename);

    if (machine_id) {
        return machine_id;
    }
    return NULL;
}

char *hifs_read_file_to_string( char filename[50] )
{
    char *buffer = 0;
    long length;
    FILE *f1 = fopen(filename, "rb");

    if (f1) {
        fseek (f1, 0, SEEK_END);
        length = ftell (f1);
        fseek (f1, 0, SEEK_SET);
        buffer = (char *) malloc(length + 1);
        if (buffer) {
            fread (buffer, 1, length, f1);
            buffer[length] = '\0';
            if (length > 0 && buffer[length - 1] == '\n') {
                buffer[length - 1] = '\0';
            }
        }
        fclose (f1);
    }

    if (buffer) {
        return buffer;
    }

    return NULL;
}

void log_to_window(WINDOW *win, const char *format, ...) {
    va_list args;
    va_start(args, format);
    mvwprintw(win, log_line, 0, format, args);
    va_end(args);
    wrefresh(win); // Refresh to update the window
    log_line++;
}