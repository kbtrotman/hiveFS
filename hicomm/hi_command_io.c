/**
 * HiveFS
 *
 * Hive Mind Filesystem
 * By K. B. Trotman
 * License: GNU GPL as of 2023
 *
 */

#include "hi_command.h"

extern PANEL *tab_panels[TAB_COUNT];
extern const char Tab_Names[TAB_COUNT][TAB_NAME_LENGTH];
extern WINDOW *tabs[TAB_COUNT];
extern WINDOW *tab_content[TAB_COUNT];
extern WINDOW *tab_headers;

void draw_tab_headers() {
    for (int i = 0; i < TAB_COUNT; i++) {
        mvwprintw(tab_headers, 0, (i * TAB_WIDTH / TAB_COUNT), Tab_Names[i]);
    }
    wrefresh(tab_headers);
}

void switch_tab(int tab_index) {
    for (int i = 0; i < TAB_COUNT; i++) {
        hide_panel(tab_panels[i]);
    }
    show_panel(tab_panels[tab_index]);
    update_panels();
    doupdate();
}

void hicomm_draw_tab_contents( void ) {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    // Create tab headers window
    tab_headers = newwin(TAB_HEADER_HEIGHT, TAB_WIDTH, 0, 0);
    draw_tab_headers();

    // Initialize tab content windows and panels
    for (int i = 0; i < TAB_COUNT; i++) {
        tab_content[i] = newwin(TAB_CONTENT_HEIGHT, TAB_WIDTH, TAB_HEADER_HEIGHT, 0);
        tab_panels[i] = new_panel(tab_content[i]);
        wprintw(tab_content[i], "Content of Tab %d\n", i + 1);
        wrefresh(tab_content[i]);
    }

    // Show the first tab initially
    switch_tab(0);
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