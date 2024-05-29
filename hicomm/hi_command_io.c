/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"


void draw_tabs(WINDOW *tabs[], int current_tab) {
    for (int i = 0; i < TAB_COUNT; i++) {
        if (i == current_tab) {
            wattron(tabs[i], A_REVERSE); // Highlight current tab
        }
        box(tabs[i], 0, 0);
        mvwprintw(tabs[i], 1, 1, "%s", Tab_Names[i]);
        if (i == current_tab) {
            wattroff(tabs[i], A_REVERSE);
        }
        wrefresh(tabs[i]);
    }
}

void draw_tab_content(WINDOW *tab_content, int tab_index) {
    werase(tab_content);
    box(tab_content, 0, 0);
    mvwprintw(tab_content, 1, 1, "%s", Tab_Names[tab_index]);
    wrefresh(tab_content);
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
    vw_printw(win, format, args);
    va_end(args);
    wrefresh(win); // Refresh to update the window
}