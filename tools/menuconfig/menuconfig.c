#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define DEBUG_H_PATH "src/debug.h"

typedef struct {
    const char* name;
    int is_numeric;     //1 if numeric option
    int editable;       //1 if editable in UI
    int value;          //toggle value (0/1) when !is_numeric
    int num_value;      //numeric value when is_numeric
} option_t;

static option_t g_options[] = {
    {"DEBUG_ENABLED", 0, 1, 0, 0},
    {"LOG_SCHED", 0, 1, 0, 0},
    {"LOG_SCHED_TABLE", 0, 1, 0, 0},
    {"LOG_SYSCALL", 0, 1, 0, 0},
    {"LOG_TICK", 0, 1, 0, 0},
    {"LOG_PROC", 0, 1, 0, 0},
    {"LOG_SCHED_DIAG", 0, 1, 0, 0},
    {"LOG_VFS", 0, 1, 0, 0},
    {"LOG_ELF", 0, 1, 0, 0},
    {"LOG_ELF_DIAG", 0, 1, 0, 0},
    {"LOG_EXEC", 0, 1, 0, 0},
    {"LOG_FAT16", 0, 1, 0, 0},
    {"LOG_ATA", 0, 1, 0, 0},
    {"FAT16_USE_READAHEAD", 0, 1, 0, 0},
    {"FAT16_READAHEAD_THRESHOLD_BYTES", 1, 1, 0, 2048},
};

static char** read_lines(const char* path, int* out_count) {
    *out_count = 0;
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    size_t cap = 128;
    char** lines = (char**)malloc(cap * sizeof(char*));
    if (!lines) {
        fclose(f);
        return NULL;
    }

    char buf[1024];
    int n = 0;
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        char* s = (char*)malloc(len + 1);
        if (!s) {
            fclose(f);
            return NULL;
        }
        memcpy(s, buf, len + 1);
        if (n >= (int)cap) {
            cap *= 2;
            char** nl = (char**)realloc(lines, cap * sizeof(char*));
            if (!nl) {
                fclose(f);
                return NULL;
            }
            lines = nl;
        }
        lines[n++] = s;
    }
    fclose(f);
    *out_count = n;
    return lines;
}

static int starts_with_define(const char* line, const char* name) {
    //match: #define <name> <value>
    const char* p = line;
    while (isspace((unsigned char)*p)) p++;
    if (strncmp(p, "#define", 7) != 0) return 0;
    p += 7;
    if (!isspace((unsigned char)*p)) return 0;
    while (isspace((unsigned char)*p)) p++;
    size_t nl = strlen(name);
    if (strncmp(p, name, nl) != 0) return 0;
    p += nl;
    if (!isspace((unsigned char)*p)) return 0;
    return 1;
}

static int parse_define_value(const char* line) {
    //assume validated starts_with_define; parse integer after name
    const char* p = strchr(line, ' ');
    if (!p) return -1;
    //skip past macro name too
    //find second whitespace transition
    while (isspace((unsigned char)*p)) p++;
    //skip token (macro name)
    while (*p && !isspace((unsigned char)*p)) p++;
    while (isspace((unsigned char)*p)) p++;
    if (!*p) return -1;
    //parse int
    int v = 0;
    if (sscanf(p, "%d", &v) == 1) return v;
    //could also handle hex if needed
    return -1;
}

static void load_values_from_lines(char** lines, int count) {
    for (size_t i = 0; i < sizeof(g_options)/sizeof(g_options[0]); i++) {
        option_t* opt = &g_options[i];
        for (int j = 0; j < count; j++) {
            if (starts_with_define(lines[j], opt->name)) {
                int v = parse_define_value(lines[j]);
                if (opt->is_numeric) {
                    if (v >= 0) opt->num_value = v;
                } else {
                    if (v == 0 || v == 1) opt->value = v;
                    else if (v >= 0) opt->value = v ? 1 : 0;
                }
                break;
            }
        }
    }
}

static int write_lines(const char* path, char** lines, int count) {
    FILE* f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < count; i++) {
        fputs(lines[i], f);
    }
    fclose(f);
    return 0;
}

static int replace_define_line(char** pline, const char* name, int value) {
    //build new line as: #ifndef NAME (no) we only replace the direct #define NAME X line
    const char* old = *pline;
    const char* p = old;
    //copy leading whitespace
    char lead[64];
    size_t li = 0;
    while (*p && isspace((unsigned char)*p) && li + 1 < sizeof(lead)) lead[li++] = *p++;
    lead[li] = '\0';
    //construct new define line
    char buf[256];
    snprintf(buf, sizeof(buf), "%s#define %s %d\n", lead, name, value);
    size_t nl = strlen(buf);
    char* n = (char*)malloc(nl + 1);
    if (!n) return -1;
    memcpy(n, buf, nl + 1);
    free(*pline);
    *pline = n;
    return 0;
}

static int apply_values_to_lines(char** lines, int count) {
    for (size_t i = 0; i < sizeof(g_options)/sizeof(g_options[0]); i++) {
        option_t* opt = &g_options[i];
        for (int j = 0; j < count; j++) {
            if (starts_with_define(lines[j], opt->name)) {
                int v = opt->is_numeric ? opt->num_value : opt->value;
                if (replace_define_line(&lines[j], opt->name, v) != 0) return -1;
                break;
            }
        }
    }
    return 0;
}

static void draw_ui(int sel) {
    clear();
    int row = 0;
    mvprintw(row++, 2, "FrostByteOS menuconfig (debug options)");
    mvprintw(row++, 2, "Up/Down: move  Space: toggle  e: edit number  +/-: adjust  s: Save  q: Quit");
    row++;
    for (size_t i = 0; i < sizeof(g_options)/sizeof(g_options[0]); i++) {
        const option_t* opt = &g_options[i];
        if (opt->is_numeric) {
            if ((int)i == sel) {
                attron(A_REVERSE);
                mvprintw(row, 2, "%s = %d%s", opt->name, opt->num_value, opt->editable ? "" : " (read-only)");
                attroff(A_REVERSE);
            } else {
                mvprintw(row, 2, "%s = %d%s", opt->name, opt->num_value, opt->editable ? "" : " (read-only)");
            }
        } else {
            char mark = opt->value ? 'X' : ' ';
            if ((int)i == sel) {
                attron(A_REVERSE);
                mvprintw(row, 2, "[%c] %s%s", mark, opt->name, opt->editable ? "" : " (read-only)");
                attroff(A_REVERSE);
            } else {
                mvprintw(row, 2, "[%c] %s%s", mark, opt->name, opt->editable ? "" : " (read-only)");
            }
        }
        row++;
    }
    refresh();
}

int main(void)
{
    int line_count = 0;
    char** lines = read_lines(DEBUG_H_PATH, &line_count);
    if (!lines) {
        fprintf(stderr, "Failed to read %s\n", DEBUG_H_PATH);
        return 1;
    }
    load_values_from_lines(lines, line_count);

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    int sel = 0;
    int nopts = (int)(sizeof(g_options)/sizeof(g_options[0]));
    int dirty = 0;
    draw_ui(sel);

    for (;;) {
        int ch = getch();
        if (ch == KEY_UP) {
            sel = (sel - 1 + nopts) % nopts;
        } else if (ch == KEY_DOWN) {
            sel = (sel + 1) % nopts;
        } else if (ch == ' ' || ch == '\n') {
            if (!g_options[sel].is_numeric && g_options[sel].editable) {
                g_options[sel].value = g_options[sel].value ? 0 : 1;
                dirty = 1;
            }
        } else if ((ch == '+' || ch == KEY_RIGHT) && g_options[sel].is_numeric && g_options[sel].editable) {
            g_options[sel].num_value += 128; //coarse step
            if (g_options[sel].num_value < 0) g_options[sel].num_value = 0;
            dirty = 1;
        } else if ((ch == '-' || ch == KEY_LEFT) && g_options[sel].is_numeric && g_options[sel].editable) {
            g_options[sel].num_value -= 128;
            if (g_options[sel].num_value < 0) g_options[sel].num_value = 0;
            dirty = 1;
        } else if ((ch == 'e' || ch == 'E') && g_options[sel].is_numeric && g_options[sel].editable) {
            echo();
            curs_set(1);
            char buf[32];
            mvprintw(LINES-2, 2, "Enter %s (integer): ", g_options[sel].name);
            clrtoeol();
            getnstr(buf, (int)sizeof(buf) - 1);
            int v = g_options[sel].num_value;
            if (sscanf(buf, "%d", &v) == 1 && v >= 0) {
                g_options[sel].num_value = v;
                dirty = 1;
            } else {
                mvprintw(LINES-2, 2, "Invalid number");
                clrtoeol();
            }
            noecho();
            curs_set(0);
        } else if (ch == 's' || ch == 'S') {
            //write values back
            if (apply_values_to_lines(lines, line_count) == 0) {
                if (write_lines(DEBUG_H_PATH, lines, line_count) == 0) {
                    dirty = 0;
                    mvprintw(LINES-2, 2, "Saved to %s", DEBUG_H_PATH);
                    clrtoeol();
                } else {
                    mvprintw(LINES-2, 2, "Failed to write %s", DEBUG_H_PATH);
                    clrtoeol();
                }
            } else {
                mvprintw(LINES-2, 2, "Failed to apply values");
                clrtoeol();
            }
        } else if (ch == 'q' || ch == 'Q') {
            if (dirty) {
                mvprintw(LINES-2, 2, "Unsaved changes (press q again to quit anyway)");
                clrtoeol();
                int ch2 = getch();
                if (ch2 == 'q' || ch2 == 'Q') break;
            } else {
                break;
            }
        }
        draw_ui(sel);
    }

    endwin();

    for (int i = 0; i < line_count; i++) free(lines[i]);
    free(lines);
    return 0;
}
