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
extern int current_tab;

void switch_tab(int tab_index) 
{
    for (int i = 0; i < TAB_COUNT; i++) {
        hide_panel(tab_panels[i]);
    }
    show_panel(tab_panels[tab_index]);
    top_panel(tab_panels[tab_index]);
    update_panels();
    doupdate();
}

void hicomm_draw_new_Content(int tab_index) 
{
    // Initialize tab content windows and panels
    for (int i = 0; i < TAB_COUNT; i++) {
        tab_headers[i] = newwin(TAB_HEADER_HEIGHT, TAB_WIDTH, 0, TAB_WIDTH * i);
        box(tab_headers[i], 0, 0);
        tab_content[i] = newwin(TAB_CONTENT_HEIGHT, TAB_CONTENT_WIDTH, TAB_HEADER_HEIGHT, 0);
        box(tab_content[i], 0, 0);
        tab_panels[i] = new_panel(tab_content[i]);
        wrefresh(tab_headers[i]);
    }
    show_panel(tab_panels[0]);
    top_panel(tab_panels[0]);
    update_panels();
    doupdate();
}

void hicomm_draw_tabs(int tab_index) 
{
    // Initialize tab headers window
    // Initialize tab content windows and panels
    for (int i = 0; i < TAB_COUNT; i++) {
        if (i == tab_index) {
            wattron(tab_headers[i], A_REVERSE);
        }
        mvwprintw(tab_headers[i], 1, 1, Tab_Names[i]);
        box(tab_headers[i], 0, 0);
        if (i == tab_index) {
            wattroff(tab_headers[i], A_REVERSE);
        }
        wrefresh(tab_headers[i]);
    }
    doupdate();
}

int show_yes_no_dialog(const char *message) {
    int startx, starty, width, height;
    WINDOW *dialog_win;
    int ch, response = 0;

    height = 7; // Dialog box height
    width = 80; // Dialog box width
    starty = 30; // Center the dialog box
    startx = (TAB_CONTENT_WIDTH - width) / 2;   // Center the dialog box

    dialog_win = newwin(height, width, starty, startx);
    box(dialog_win, 0, 0);
    mvwprintw(dialog_win, 2, 2, "%s", message);
    mvwprintw(dialog_win, 4, 2, "Press 'y' for Yes or 'n' for No");
    wrefresh(dialog_win);

    while (1) {
        ch = wgetch(dialog_win);
        if (ch == 'y' || ch == 'Y') {
            response = 1;
            break;
        } else if (ch == 'n' || ch == 'N') {
            response = 0;
            break;
        }
    }

    delwin(dialog_win);

    update_panels(); // Update the panels after closing the dialog
    doupdate();

    return response;
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

void hifs_set_log(void) {
    log_line += 2;
    if (log_line >= TAB_CONTENT_HEIGHT) {
        log_line = 1;
        werase(tab_content[0]);
    }
    box(tab_content[0], 0, 0);
    show_panel(tab_panels[current_tab]);
    update_panels();
    doupdate();
}