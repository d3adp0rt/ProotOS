/*
 * ProotOS — small full-screen text editor (nano-like).
 * Usage: edit FILE
 * Ctrl+S save, Ctrl+Q quit, arrows/pgup/pgdn move, Enter/Backspace/Tab.
 */
#include "libc.h"
#include <stdio.h>

#define MAXLINES 256
#define MAXCOLS 512
#define BUF_SIZE 65536

static char g_lines[MAXLINES][MAXCOLS];
static int g_nlines;
static int g_cx, g_cy;
static int g_top;
static int g_dirty;
static char g_path[256];
static int g_cols = 80, g_rows = 25;

static void editor_rows(int *content) {
    int r = g_rows - 2;
    if (r < 1) r = 1;
    *content = r;
}

static void clamp_cursor(void) {
    if (g_cy < 0) g_cy = 0;
    if (g_cy >= g_nlines) g_cy = g_nlines - 1;
    int len = (int)strlen(g_lines[g_cy]);
    if (g_cx < 0) g_cx = 0;
    if (g_cx > len) g_cx = len;
}

static void ensure_visible(void) {
    int content;
    editor_rows(&content);
    if (g_cy < g_top) g_top = g_cy;
    if (g_cy >= g_top + content) g_top = g_cy - content + 1;
    if (g_top < 0) g_top = 0;
    if (g_top > g_nlines - 1) g_top = g_nlines > 0 ? g_nlines - 1 : 0;
}

static void editor_draw(void) {
    printf("\x1b[2J\x1b[1;1H");
    /* header */
    printf("ProotOS editor  %s%s", g_path, g_dirty ? "  [Modified]" : "");
    int content;
    editor_rows(&content);
    int width = g_cols - 1;
    for (int r = 0; r < content; r++) {
        int line = g_top + r;
        printf("\x1b[%d;1H", r + 2);
        if (line < g_nlines) {
            int len = (int)strlen(g_lines[line]);
            for (int i = 0; i < width; i++) {
                char c = i < len ? g_lines[line][i] : ' ';
                putchar(c);
            }
        } else {
            for (int i = 0; i < width; i++)
                putchar(' ');
        }
    }
    printf("\x1b[%d;1H", g_rows);
    char status[200];
    snprintf(status, sizeof(status),
             "L%d:C%d  Ctrl+S save  Ctrl+Q quit  arrows move  PgUp/PgDn scroll",
             g_cy + 1, g_cx + 1);
    int sl = (int)strlen(status);
    for (int i = 0; i < width; i++)
        putchar(i < sl ? status[i] : ' ');
    int srow = 2 + (g_cy - g_top);
    int scol = 1 + g_cx;
    if (scol > width) scol = width;
    printf("\x1b[%d;%dH", srow, scol);
}

static void editor_save(void) {
    static char out[BUF_SIZE];
    size_t n = 0;
    for (int i = 0; i < g_nlines; i++) {
        int len = (int)strlen(g_lines[i]);
        for (int j = 0; j < len && n + 2 < sizeof(out); j++)
            out[n++] = g_lines[i][j];
        if (n + 2 < sizeof(out))
            out[n++] = '\n';
    }
    unlink(g_path);
    int fd = open(g_path, 0x242, 0644);
    if (fd >= 0) {
        write(fd, out, n);
        close(fd);
        g_dirty = 0;
    }
}

static void editor_load(const char *path) {
    strcpy(g_path, path);
    int fd = open(path, 0, 0);
    if (fd < 0) {
        g_nlines = 1;
        g_lines[0][0] = 0;
        return;
    }
    static char buf[BUF_SIZE];
    long n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        g_nlines = 1;
        g_lines[0][0] = 0;
        return;
    }
    int line = 0, col = 0;
    for (long i = 0; i < n && line < MAXLINES; i++) {
        char c = buf[i];
        if (c == '\r') continue;
        if (c == '\n') {
            g_lines[line][col] = 0;
            line++;
            col = 0;
            continue;
        }
        if (col < MAXCOLS - 1)
            g_lines[line][col++] = c;
    }
    if (line < MAXLINES && col > 0) {
        g_lines[line][col] = 0;
        line++;
    }
    g_nlines = line > 0 ? line : 1;
}

static void insert_char(char c) {
    char *ln = g_lines[g_cy];
    int len = (int)strlen(ln);
    if (len >= MAXCOLS - 1) return;
    for (int i = len; i >= g_cx; i--)
        ln[i + 1] = ln[i];
    ln[g_cx] = c;
    g_cx++;
    g_dirty = 1;
}

static void newline(void) {
    if (g_nlines >= MAXLINES) return;
    for (int i = g_nlines; i > g_cy + 1; i--)
        strcpy(g_lines[i], g_lines[i - 1]);
    char *ln = g_lines[g_cy];
    strcpy(g_lines[g_cy + 1], ln + g_cx);
    ln[g_cx] = 0;
    g_nlines++;
    g_cy++;
    g_cx = 0;
    g_dirty = 1;
}

static void backspace(void) {
    char *ln = g_lines[g_cy];
    int len = (int)strlen(ln);
    if (g_cx > 0) {
        for (int i = g_cx - 1; i < len; i++)
            ln[i] = ln[i + 1];
        g_cx--;
        g_dirty = 1;
    } else if (g_cy > 0) {
        char *prev = g_lines[g_cy - 1];
        int plen = (int)strlen(prev);
        if (plen + len < MAXCOLS) {
            strcat(prev, ln);
            for (int i = g_cy; i < g_nlines - 1; i++)
                strcpy(g_lines[i], g_lines[i + 1]);
            g_nlines--;
            g_cy--;
            g_cx = plen;
            g_dirty = 1;
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: edit FILE\n");
        return 1;
    }
    /* Fit both the 80x45 kernel console and the 78x30 GUI terminal. */
    g_cols = 78;
    g_rows = 24;
    int kc = 0, kr = 0;
    if (console_info(&kc, &kr) == 0) {
        if (kc > 0 && kc - 1 < g_cols) g_cols = kc - 1;
        if (kr > 0 && kr - 2 < g_rows) g_rows = kr - 2;
    }
    if (g_cols < 20) g_cols = 20;
    if (g_rows < 5) g_rows = 5;
    editor_load(argv[1]);
    g_cx = g_cy = g_top = 0;
    g_dirty = 0;

    int kbd = 0;
    int kbd_nonblock = 0;
    if (fcntl(0, F_GETFL, 0) < 0) {
        kbd = open("/dev/kbd", 0, 0);
        kbd_nonblock = 1;
    }
    if (kbd < 0) {
        printf("edit: no input device\n");
        return 1;
    }
    editor_draw();
    for (;;) {
        char c;
        long n = read(kbd, &c, 1);
        if (n <= 0) {
            if (kbd_nonblock)
                sleep_ms(10);
            else
                break;
            continue;
        }
        unsigned char u = (unsigned char)c;
        if (u == 19) { /* Ctrl+S */
            editor_save();
            editor_draw();
            continue;
        }
        if (u == 17) /* Ctrl+Q */
            break;
        if (u == 0x80) {
            g_cy--;
        } else if (u == 0x81) {
            g_cy++;
        } else if (u == 0x84) {
            g_cx--;
        } else if (u == 0x85) {
            g_cx++;
        } else if (u == 0x82) {
            int content;
            editor_rows(&content);
            g_cy -= content;
        } else if (u == 0x83) {
            int content;
            editor_rows(&content);
            g_cy += content;
        } else if (u == '\n' || u == '\r') {
            newline();
        } else if (u == 8 || u == 127) {
            backspace();
        } else if (u == '\t') {
            int next = (g_cx + 4) & ~3;
            while (g_cx < next) insert_char(' ');
        } else if (u >= 32 && u < 127) {
            insert_char((char)u);
        }
        clamp_cursor();
        ensure_visible();
        editor_draw();
    }
    printf("\x1b[2J\x1b[1;1H");
    if (kbd > 2)
        close(kbd);
    return 0;
}
