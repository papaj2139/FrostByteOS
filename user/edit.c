#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

//TTY mode bits
#define TTY_MODE_CANON (1u << 0)
#define TTY_MODE_ECHO  (1u << 1)
#define TTY_IOCTL_SET_MODE  1u
#define TTY_IOCTL_GET_MODE  2u

#define MAX_LINES 1000
#define MAX_LINE_LEN 256
#define ESC 27

static char* lines[MAX_LINES];
static int num_lines = 0;
static int cursor_line = 0;
static int cursor_col = 0;
static int top_line = 0;
static int screen_rows = 20;
static int modified = 0;
static char filename[256] = {0};

//input buffer for multi-byte reads
static char input_buf[64];
static int input_pos = 0;
static int input_len = 0;

//ANSI escape codes
static void clear_screen(void) {
    write(1, "\033[2J\033[H", 7);
}

static void move_cursor(int row, int col) {
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "\033[%d;%dH", row + 1, col + 1);
    write(1, buf, len);
}

static void set_color(int fg) {
    char buf[16];
    int len = snprintf(buf, sizeof(buf), "\033[%dm", fg);
    write(1, buf, len);
}

static void reset_color(void) {
    write(1, "\033[0m", 4);
}

//sllocate a new line with fixed MAX_LINE_LEN size
static char* alloc_line(const char* str) {
    char* line = malloc(MAX_LINE_LEN);
    if (!line) return NULL;
    if (str) {
        strncpy(line, str, MAX_LINE_LEN - 1);
        line[MAX_LINE_LEN - 1] = '\0';
    } else {
        line[0] = '\0';
    }
    return line;
}

//insert empty line at position
static int insert_line(int pos, const char* str) {
    if (num_lines >= MAX_LINES) return -1;
    if (pos < 0) pos = 0;
    if (pos > num_lines) pos = num_lines;

    //shift lines down
    for (int i = num_lines; i > pos; i--) {
        lines[i] = lines[i - 1];
    }

    lines[pos] = alloc_line(str);
    if (!lines[pos]) return -1;
    num_lines++;
    modified = 1;
    return 0;
}

//delete line at position
static void delete_line(int pos) {
    if (pos < 0 || pos >= num_lines) return;
    free(lines[pos]);

    // Shift lines up
    for (int i = pos; i < num_lines - 1; i++) {
        lines[i] = lines[i + 1];
    }
    num_lines--;
    modified = 1;
}

//load file
static int load_file(const char* path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    char buf[4096];
    char line_buf[MAX_LINE_LEN];
    int line_pos = 0;

    while (1) {
        int n = read(fd, buf, sizeof(buf));
        if (n <= 0) break;

        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                line_buf[line_pos] = '\0';
                insert_line(num_lines, line_buf);
                line_pos = 0;
            } else if (line_pos < MAX_LINE_LEN - 1) {
                line_buf[line_pos++] = c;
            }
        }
    }

    //add last line if needed
    if (line_pos > 0) {
        line_buf[line_pos] = '\0';
        insert_line(num_lines, line_buf);
    }

    close(fd);
    modified = 0;
    return 0;
}

//save file
static int save_file(const char* path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC);
    if (fd < 0) return -1;

    for (int i = 0; i < num_lines; i++) {
        write(fd, lines[i], strlen(lines[i]));
        write(fd, "\n", 1);
    }

    close(fd);
    modified = 0;
    return 0;
}

//draw status line
static void draw_status(const char* msg) {
    move_cursor(screen_rows, 0);
    set_color(7);  //inverse
    write(1, "\033[7m", 4);

    char status[128];
    int len = snprintf(status, sizeof(status), " %s %s Line %d/%d Col %d ",
                      modified ? "[+]" : "   ",
                      filename[0] ? filename : "[No Name]",
                      cursor_line + 1, num_lines, cursor_col + 1);
    write(1, status, len);

    //use clear to end of line instead of padding
    write(1, "\033[K", 3);

    reset_color();

    //help line
    move_cursor(screen_rows + 1, 0);
    const char* help = "^S Save  ^Q Quit  ^K Delete Line  ^O Insert Line  ^H Help";
    write(1, help, strlen(help));
    write(1, "\033[K", 3);  //clear to end

    //message line
    if (msg) {
        move_cursor(screen_rows + 2, 0);
        set_color(33);  //yellow
        write(1, msg, strlen(msg));
        write(1, "\033[K", 3);
        reset_color();
    }
}

//draw screen
static void draw_screen(const char* msg) {
    clear_screen();

    //draw lines
    for (int i = 0; i < screen_rows; i++) {
        int line_idx = top_line + i;
        move_cursor(i, 0);

        if (line_idx < num_lines) {
            //line number
            set_color(90);  //gray
            char num[8];
            int nlen = snprintf(num, sizeof(num), "%3d ", line_idx + 1);
            write(1, num, nlen);
            reset_color();

            //line content
            write(1, lines[line_idx], strlen(lines[line_idx]));
        } else {
            set_color(90);
            write(1, "~", 1);
            reset_color();
        }
        write(1, "\033[K", 3);  //clear to end of line
    }

    draw_status(msg);

    //position cursor
    int screen_y = cursor_line - top_line;
    move_cursor(screen_y, cursor_col + 4);  //+4 for line numbers
}

//read single character with buffering
static int read_char(void) {
    //return from buffer if available
    if (input_pos < input_len) {
        return (unsigned char)input_buf[input_pos++];
    }

    //read new data
    input_len = read(0, input_buf, sizeof(input_buf));
    if (input_len <= 0) {
        input_len = 0;
        input_pos = 0;
        return -1;
    }

    input_pos = 1;
    return (unsigned char)input_buf[0];
}

//handle input
static int handle_input(void) {
    int c = read_char();
    if (c < 0) return 0;

    //backspace
    if (c == 127 || c == 8) {
        if (cursor_col > 0) {
            char* line = lines[cursor_line];
            int len = strlen(line);
            for (int i = cursor_col - 1; i < len; i++) {
                line[i] = line[i + 1];
            }
            cursor_col--;
            modified = 1;
        } else if (cursor_line > 0) {
            //join with previous line
            char* prev = lines[cursor_line - 1];
            char* curr = lines[cursor_line];
            int prev_len = strlen(prev);

            if (prev_len + strlen(curr) < MAX_LINE_LEN) {
                strcat(prev, curr);
                delete_line(cursor_line);
                cursor_line--;
                cursor_col = prev_len;
            }
        }
        draw_screen(NULL);
        return 0;
    }

    //control characters
    if (c == 17) {  //ctrl+q
        if (modified) {
            draw_screen("Warning: File modified! Press Ctrl+Q again to quit without saving.");
            int c2 = read_char();
            if (c2 == 17) return 1;  //quit
            return 0;
        }
        return 1;  //quit
    }

    if (c == 19) {  //ctrl+s
        if (filename[0]) {
            if (save_file(filename) == 0) {
                draw_screen("Saved!");
            } else {
                draw_screen("Error: Could not save file!");
            }
        } else {
            draw_screen("No filename specified!");
        }
        return 0;
    }

    if (c == 11) {  //cltrk +k
        if (num_lines > 0) {
            delete_line(cursor_line);
            if (cursor_line >= num_lines && cursor_line > 0) {
                cursor_line--;
            }
        }
        draw_screen(NULL);
        return 0;
    }

    if (c == 15) {  //ctrl + o
        insert_line(cursor_line, "");
        draw_screen(NULL);
        return 0;
    }

    //arrows
    if (c == ESC) {
        int c2 = read_char();
        if (c2 == '[') {
            int c3 = read_char();
            if (c3 == 'A') {  //up
                if (cursor_line > 0) {
                    cursor_line--;
                    if (cursor_line < top_line) top_line = cursor_line;
                    if (cursor_col > (int)strlen(lines[cursor_line])) {
                        cursor_col = strlen(lines[cursor_line]);
                    }
                }
            } else if (c3 == 'B') {  //down
                if (cursor_line < num_lines - 1) {
                    cursor_line++;
                    if (cursor_line >= top_line + screen_rows) {
                        top_line = cursor_line - screen_rows + 1;
                    }
                    if (cursor_col > (int)strlen(lines[cursor_line])) {
                        cursor_col = strlen(lines[cursor_line]);
                    }
                }
            } else if (c3 == 'C') {  //right
                if (cursor_line < num_lines) {
                    if (cursor_col < (int)strlen(lines[cursor_line])) {
                        cursor_col++;
                    }
                }
            } else if (c3 == 'D') {  //left
                if (cursor_col > 0) cursor_col--;
            }
        }
        draw_screen(NULL);
        return 0;
    }

    // Printable characters
    if (num_lines == 0) {
        insert_line(0, "");
    }

    if (c >= 32 && c < 127) {
        char* line = lines[cursor_line];
        int len = strlen(line);

        if (len < MAX_LINE_LEN - 1) {
            //insert character
            for (int i = len; i >= cursor_col; i--) {
                line[i + 1] = line[i];
            }
            line[cursor_col] = c;
            cursor_col++;
            modified = 1;
        }
        draw_screen(NULL);
        return 0;
    }

    //enter
    if (c == '\n' || c == '\r') {
        char* line = lines[cursor_line];
        char rest[MAX_LINE_LEN];
        strcpy(rest, line + cursor_col);
        line[cursor_col] = '\0';

        insert_line(cursor_line + 1, rest);
        cursor_line++;
        cursor_col = 0;

        if (cursor_line >= top_line + screen_rows) {
            top_line++;
        }

        draw_screen(NULL);
        return 0;
    }

    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1) {
        strncpy(filename, argv[1], sizeof(filename) - 1);
        if (load_file(filename) < 0) {
            //file doesnt exist so start with empty buffer
            insert_line(0, "");
        }
    } else {
        insert_line(0, "");
    }

    //save old TTY mode and set raw mode
    uint32_t old_mode = TTY_MODE_CANON | TTY_MODE_ECHO;
    int ret = ioctl(0, TTY_IOCTL_GET_MODE, &old_mode);
    if (ret < 0) {
        write(2, "Warning: Failed to get TTY mode\n", 33);
    }

    uint32_t raw_mode = 0;
    ret = ioctl(0, TTY_IOCTL_SET_MODE, &raw_mode);
    if (ret < 0) {
        write(2, "Warning: Failed to set raw mode\n", 33);
    }

    //show cursor
    write(1, "\033[?25h", 6);

    draw_screen("Welcome to EDIT! Press Ctrl+H for help.");

    while (1) {
        if (handle_input()) break;
    }

    //restore TTY mode
    ioctl(0, TTY_IOCTL_SET_MODE, &old_mode);

    clear_screen();
    return 0;
}
