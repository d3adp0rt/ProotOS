/*
 * ProotOS — M10d userspace window manager.
 *
 * Maps the GOP framebuffer through SYS_FB_MAP, draws overlapping windows,
 * handles PS/2 mouse clicks, focus, z-order, dragging and resizing, and runs
 * shell terminals over pipes. Usage: gui [seconds] (default: until ESC).
 */
#include "libc.h"
#include "../third_party/fonts/font8x8_basic.h"

#define MAX_WINS 7
#define TITLE_H 34
#define TASKBAR_H 38
#define TERM_COLS 78
#define TERM_ROWS 30
#define HIST_ROWS 300
#define LINE_H 10
#define MIN_W 260
#define MIN_H 160

enum {
    W_DESK = 0,
    W_SYSTEM,
    W_NOTES,
    W_FILES,
    W_EDIT,
    W_TERM,
};

#define FILES_MAX 12
#define FILE_ROWS 24
#define FILE_COLS 46

struct term_state {
    char grid[TERM_ROWS][TERM_COLS];
    char hist[HIST_ROWS][TERM_COLS];
    int hist_count;
    int offset;
    int row, col, esc, bytes;
    int in_fd, out_fd, pid;
    int used;
    int sel_a, sel_b;
    int disp_line[72];
    int disp_count;
};

struct window {
    int x, y, w, h;
    const char *title;
    int visible;
    int kind;
};

#define BACK_MAX_W 1280
#define BACK_MAX_H 720
static uint32_t *g_fb;
static struct fb_info g_info;
static int g_rgb;
static uint32_t g_back[BACK_MAX_W * BACK_MAX_H];
static uint32_t *g_draw;
static uint32_t g_draw_stride;
static int g_back_ok;

static struct window g_wins[MAX_WINS] = {
    {140, 110, 560, 320, "ProotOS Desktop", 1, W_DESK},
    {420, 300, 400, 220, "System", 1, W_SYSTEM},
    {900, 120, 300, 200, "Notes", 1, W_NOTES},
    {40, 420, 620, 270, "Files", 0, W_FILES},
    {300, 160, 680, 360, "Terminal", 0, W_TERM},
    {300, 160, 680, 360, "Terminal", 0, W_TERM},
    {300, 160, 680, 360, "Terminal", 0, W_TERM},
};
static int g_order[MAX_WINS] = {0, 1, 2, 3, 4, 5, 6};
static int g_active = 2;
static struct term_state g_terms[MAX_WINS];
static int g_term_total;

static char g_files_path[256] = "/";
static char g_files_names[FILES_MAX][64];
static int g_files_types[FILES_MAX];
static int g_files_count;
static int g_files_sel = -1;
static char g_files_preview[FILE_ROWS][FILE_COLS + 1];
static int g_files_plines;
static char g_files_selpath[384];
static int g_sel_win = -1;
static char g_clipboard[4096];
static int g_clipboard_len;

#define EDIT_LINES 64
#define EDIT_COLS 96
static char g_edit_title[128] = "Editor";
static char g_edit_path[384];
static char g_edit_lines[EDIT_LINES][EDIT_COLS];
static int g_edit_nlines;
static int g_edit_cx, g_edit_cy;
static int g_edit_scroll;
static int g_edit_dirty;

#define UNDO_DEPTH 8
struct undo_state {
    char lines[EDIT_LINES][EDIT_COLS];
    int nlines, cx, cy;
};
static struct undo_state g_undo[UNDO_DEPTH];
static int g_undo_count;

struct clip {
    int on, x, y, w, h;
};
static struct clip g_clip;

struct rect {
    int x, y, w, h;
};
static struct rect g_damage;
static int g_damage_on;

static uint32_t rgba(uint32_t rgb) {
    if (g_rgb)
        return ((rgb & 0xff) << 16) | (rgb & 0xff00) | ((rgb >> 16) & 0xff);
    return rgb & 0x00ffffff;
}

static void put(int x, int y, uint32_t rgb) {
    if (x < 0 || y < 0 || (uint32_t)x >= g_info.width || (uint32_t)y >= g_info.height)
        return;
    if (g_clip.on && (x < g_clip.x || y < g_clip.y || x >= g_clip.x + g_clip.w ||
                      y >= g_clip.y + g_clip.h))
        return;
    g_draw[(uint64_t)y * g_draw_stride + (uint32_t)x] = rgba(rgb);
}

static uint32_t get(int x, int y) {
    return g_fb[(uint64_t)y * (g_info.pitch / 4) + (uint32_t)x];
}

static void fill(int x, int y, int w, int h, uint32_t rgb) {
    uint32_t v = rgba(rgb);
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (g_clip.on) {
        if (x0 < g_clip.x) x0 = g_clip.x;
        if (y0 < g_clip.y) y0 = g_clip.y;
        if (x1 > g_clip.x + g_clip.w) x1 = g_clip.x + g_clip.w;
        if (y1 > g_clip.y + g_clip.h) y1 = g_clip.y + g_clip.h;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)g_info.width) x1 = (int)g_info.width;
    if (y1 > (int)g_info.height) y1 = (int)g_info.height;
    for (int j = y0; j < y1; j++)
        for (int i = x0; i < x1; i++)
            g_draw[(uint64_t)j * g_draw_stride + (uint32_t)i] = v;
}

static void frame(int x, int y, int w, int h, uint32_t rgb) {
    fill(x, y, w, 2, rgb);
    fill(x, y + h - 2, w, 2, rgb);
    fill(x, y, 2, h, rgb);
    fill(x + w - 2, y, 2, h, rgb);
}

static void text(int x, int y, const char *s, uint32_t rgb, int scale) {
    for (int n = 0; s[n]; n++) {
        unsigned char ch = (unsigned char)s[n];
        if (ch >= 128)
            ch = '?';
        for (int row = 0; row < 8; row++) {
            unsigned char bits = (unsigned char)font8x8_basic[ch][row];
            for (int col = 0; col < 8; col++) {
                if (bits & (1u << col))
                    fill(x + (n * 8 + col) * scale, y + row * scale, scale, scale, rgb);
            }
        }
    }
}

static void term_init(int idx) {
    struct term_state *t = &g_terms[idx];
    for (int r = 0; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++)
            t->grid[r][c] = ' ';
    t->row = t->col = t->esc = t->bytes = 0;
    t->hist_count = 0;
    t->offset = 0;
    t->in_fd = t->out_fd = -1;
    t->pid = -1;
    t->used = 1;
    t->sel_a = t->sel_b = -1;
    t->disp_count = 0;
}

static void term_scroll(int idx) {
    struct term_state *t = &g_terms[idx];
    int slot = t->hist_count % HIST_ROWS;
    for (int c = 0; c < TERM_COLS; c++)
        t->hist[slot][c] = t->grid[0][c];
    t->hist_count++;
    for (int r = 1; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++)
            t->grid[r - 1][c] = t->grid[r][c];
    for (int c = 0; c < TERM_COLS; c++)
        t->grid[TERM_ROWS - 1][c] = ' ';
    t->row = TERM_ROWS - 1;
}

static void term_newline(int idx) {
    struct term_state *t = &g_terms[idx];
    t->col = 0;
    t->row++;
    if (t->row >= TERM_ROWS)
        term_scroll(idx);
}

static void term_putc(int idx, char ch) {
    struct term_state *t = &g_terms[idx];
    if (t->esc) {
        if (t->esc == 1 && ch == '[') { t->esc = 2; return; }
        t->esc = 0;
        return;
    }
    if (ch == 27) { t->esc = 1; return; }
    if (ch == '\n') { term_newline(idx); return; }
    if (ch == '\r') { t->col = 0; return; }
    if (ch == '\b') { if (t->col > 0) t->col--; return; }
    if ((unsigned char)ch < 32) return;
    if (t->col >= TERM_COLS)
        term_newline(idx);
    t->grid[t->row][t->col++] = ch;
}

static void term_feed(int idx, const char *buf, int n) {
    struct term_state *t = &g_terms[idx];
    t->bytes += n;
    g_term_total += n;
    for (int i = 0; i < n; i++)
        term_putc(idx, buf[i]);
}

static char *term_line(int idx, int line) {
    struct term_state *t = &g_terms[idx];
    if (line < 0)
        return 0;
    if (line < t->hist_count)
        return t->hist[line % HIST_ROWS];
    line -= t->hist_count;
    if (line < TERM_ROWS)
        return t->grid[line];
    return 0;
}

static int term_len(int idx, int line) {
    char *p = term_line(idx, line);
    if (!p)
        return 0;
    int n = TERM_COLS;
    while (n > 0 && p[n - 1] == ' ')
        n--;
    return n;
}

static void terminal_copy(int idx) {
    struct term_state *t = &g_terms[idx];
    int lo = t->sel_a, hi = t->sel_b;
    if (lo < 0 || hi < 0)
        return;
    if (lo > hi) { int tmp = lo; lo = hi; hi = tmp; }
    g_clipboard_len = 0;
    for (int line = lo; line <= hi; line++) {
        char *p = term_line(idx, line);
        if (!p)
            continue;
        int len = term_len(idx, line);
        for (int i = 0; i < len && g_clipboard_len < (int)sizeof(g_clipboard) - 2; i++)
            g_clipboard[g_clipboard_len++] = p[i];
        if (g_clipboard_len < (int)sizeof(g_clipboard) - 1)
            g_clipboard[g_clipboard_len++] = '\n';
    }
    g_clipboard[g_clipboard_len] = 0;
}

static void draw_terminal(int idx, int x, int y, int w, int h) {
    struct term_state *t = &g_terms[idx];
    int cx = x + 12, cy = y + 8;
    int cols = (w - 24) / 8;
    if (cols < 10) cols = 10;
    if (cols > TERM_COLS) cols = TERM_COLS;
    int rows = (h - 16) / LINE_H;
    if (rows < 1) return;
    if (rows > 72) rows = 72;

    int last = t->hist_count + t->row;
    long total = 0;
    for (int line = 0; line <= last; line++) {
        int len = term_len(idx, line);
        int segs = (len + cols - 1) / cols;
        if (segs < 1) segs = 1;
        total += segs;
    }
    long maxoff = total - rows;
    if (maxoff < 0) maxoff = 0;
    if (t->offset > maxoff) t->offset = (int)maxoff;
    if (t->offset < 0) t->offset = 0;
    long start = total - rows - t->offset;
    if (start < 0) start = 0;

    int sel_lo = t->sel_a, sel_hi = t->sel_b;
    if (sel_lo > sel_hi) { int tmp = sel_lo; sel_lo = sel_hi; sel_hi = tmp; }

    long drow = 0;
    int dy = 0;
    int cur_dr = -1, cur_dc = 0;
    t->disp_count = 0;
    for (int line = 0; line <= last && dy < rows; line++) {
        int len = term_len(idx, line);
        int segs = (len + cols - 1) / cols;
        if (segs < 1) segs = 1;
        for (int seg = 0; seg < segs && dy < rows; seg++) {
            if (drow < start) {
                drow++;
                continue;
            }
            int startc = seg * cols;
            int n = len - startc;
            if (n < 0) n = 0;
            if (n > cols) n = cols;
            if (sel_lo >= 0 && line >= sel_lo && line <= sel_hi)
                fill(cx, cy + dy * LINE_H, cols * 8, 8, 0x1e3a8a);
            if (n > 0) {
                char tmp[TERM_COLS + 1];
                char *src = term_line(idx, line) + startc;
                for (int i = 0; i < n; i++)
                    tmp[i] = src[i];
                tmp[n] = 0;
                text(cx, cy + dy * LINE_H, tmp, 0x9becb8, 1);
            }
            if (t->offset == 0 && line == last && t->col >= startc &&
                (t->col < startc + cols || seg == segs - 1)) {
                cur_dr = dy;
                cur_dc = t->col - startc;
                if (cur_dc >= cols) cur_dc = cols - 1;
            }
            t->disp_line[dy] = line;
            dy++;
            drow++;
        }
    }
    t->disp_count = dy;
    if (g_active == idx && cur_dr >= 0 && cur_dr < rows)
        fill(cx + cur_dc * 8, cy + cur_dr * LINE_H + 8, 8, 2, 0x9becb8);
}

static void desktop(void) {
    int x0 = 0, y0 = 0, x1 = (int)g_info.width, y1 = (int)g_info.height;
    if (g_clip.on) {
        x0 = g_clip.x;
        y0 = g_clip.y;
        x1 = g_clip.x + g_clip.w;
        y1 = g_clip.y + g_clip.h;
    }
    for (int y = y0; y < y1; y++) {
        uint32_t t = (uint32_t)y * 255 / g_info.height;
        uint32_t r = 10 + t / 6, g = 18 + t / 3, b = 34 + t / 2;
        for (int x = x0; x < x1; x++)
            put(x, y, (r << 16) | (g << 8) | b);
    }
}

static void files_join(const char *name, char *out, size_t cap) {
    size_t n = 0;
    const char *p = g_files_path;
    while (*p && n + 1 < cap)
        out[n++] = *p++;
    if (n == 0 || out[n - 1] != '/') {
        if (n + 1 < cap)
            out[n++] = '/';
    }
    while (*name && n + 1 < cap)
        out[n++] = *name++;
    out[n] = 0;
}

static void files_refresh(void) {
    g_files_count = 0;
    g_files_sel = -1;
    g_files_plines = 0;
    char name[64];
    int index = 0;
    while (g_files_count < FILES_MAX) {
        int r = readdir(g_files_path, (unsigned)index++, name, sizeof(name));
        if (r < 0)
            break;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
            continue;
        strcpy(g_files_names[g_files_count], name);
        g_files_types[g_files_count] = r;
        g_files_count++;
    }
}

static void files_preview(const char *full) {
    g_files_plines = 0;
    int fd = open(full, 0, 0);
    if (fd < 0) {
        strcpy(g_files_preview[0], "(cannot open)");
        g_files_plines = 1;
        return;
    }
    static char buf[4096];
    long n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) {
        strcpy(g_files_preview[0], "(empty)");
        g_files_plines = 1;
        return;
    }
    int line = 0, col = 0;
    for (long i = 0; i < n && line < FILE_ROWS; i++) {
        char c = buf[i];
        if (c == '\r')
            continue;
        if (c == '\n') {
            g_files_preview[line][col] = 0;
            line++;
            col = 0;
            continue;
        }
        if (col < FILE_COLS)
            g_files_preview[line][col++] = c;
    }
    if (line < FILE_ROWS && col > 0) {
        g_files_preview[line][col] = 0;
        line++;
    }
    g_files_plines = line;
    if (!g_files_plines) {
        strcpy(g_files_preview[0], "(empty)");
        g_files_plines = 1;
    }
}

static void files_up(void) {
    if (strcmp(g_files_path, "/") == 0)
        return;
    int last = -1;
    for (int i = 0; g_files_path[i]; i++)
        if (g_files_path[i] == '/')
            last = i;
    if (last <= 0) {
        strcpy(g_files_path, "/");
    } else {
        g_files_path[last] = 0;
    }
    files_refresh();
}

static void files_select(int idx) {
    if (idx < 0 || idx >= g_files_count)
        return;
    g_files_sel = idx;
    char full[384];
    files_join(g_files_names[idx], full, sizeof(full));
    if (g_files_types[idx] == 2) {
        char tmp[256];
        strcpy(tmp, full);
        strcpy(g_files_path, tmp);
        g_files_selpath[0] = 0;
        files_refresh();
    } else {
        strcpy(g_files_selpath, full);
        files_preview(full);
    }
}

static void editor_open(const char *path);

static void files_click(int x, int y, int wx, int wy, int ww, int wh) {
    int lx = x - wx;
    int ly = y - wy;
    if (lx >= ww - 104 && ly >= wh - 38) {
        if (g_files_selpath[0])
            editor_open(g_files_selpath);
        return;
    }
    if (lx < 8 || lx >= 212 || ly < 30)
        return;
    int row = (ly - 30) / 13;
    if (row == 0)
        files_up();
    else if (row >= 1 && row <= g_files_count)
        files_select(row - 1);
}

static void draw_files(int x, int y, int w, int h) {
    (void)h;
    text(x + 8, y + 8, g_files_path, 0x93c5fd, 1);
    int row_y = y + 30;
    text(x + 8, row_y, "..", 0x94a3b8, 1);
    for (int i = 0; i < g_files_count; i++) {
        int ry = row_y + (i + 1) * 13;
        if (i == g_files_sel)
            fill(x + 4, ry - 2, 204, 13, 0x1d4ed8);
        uint32_t col = g_files_types[i] == 2 ? 0xfde68a : 0xe5e7eb;
        char label[64];
        strcpy(label, g_files_names[i]);
        if (g_files_types[i] == 2)
            strcat(label, "/");
        text(x + 8, ry, label, col, 1);
    }
    fill(x + 214, y + 6, 1, h - 12, 0x334155);
    text(x + 224, y + 8, "preview", 0x93c5fd, 1);
    for (int i = 0; i < g_files_plines && i < FILE_ROWS; i++)
        text(x + 224, y + 26 + i * 10, g_files_preview[i], 0xa7f3d0, 1);
    uint32_t bcol = g_files_selpath[0] ? 0x2563eb : 0x1f2937;
    fill(x + w - 100, y + h - 34, 92, 26, bcol);
    frame(x + w - 100, y + h - 34, 92, 26, g_files_selpath[0] ? 0x60a5fa : 0x374151);
    text(x + w - 78, y + h - 26, "Edit", 0xffffff, 1);
}

#define EDIT_WIN 2
static void raise_window(int idx);
static void editor_ensure_visible(void);

static int editor_rows(void) {
    int h = g_wins[EDIT_WIN].h - TITLE_H - 16;
    int rows = h / LINE_H;
    if (rows < 1) rows = 1;
    if (rows > EDIT_LINES) rows = EDIT_LINES;
    return rows;
}

static void editor_title(void) {
    const char *base = g_edit_path;
    for (int i = 0; g_edit_path[i]; i++)
        if (g_edit_path[i] == '/')
            base = g_edit_path + i + 1;
    strcpy(g_edit_title, "Edit: ");
    strcat(g_edit_title, base);
    if (g_edit_dirty && strlen(g_edit_title) < sizeof(g_edit_title) - 3)
        strcat(g_edit_title, " *");
}

static void editor_snapshot(void) {
    if (g_undo_count == UNDO_DEPTH) {
        for (int i = 1; i < UNDO_DEPTH; i++)
            g_undo[i - 1] = g_undo[i];
        g_undo_count--;
    }
    struct undo_state *u = &g_undo[g_undo_count++];
    memcpy(u->lines, g_edit_lines, sizeof(g_edit_lines));
    u->nlines = g_edit_nlines;
    u->cx = g_edit_cx;
    u->cy = g_edit_cy;
}

static void editor_undo(void) {
    if (!g_undo_count)
        return;
    struct undo_state *u = &g_undo[--g_undo_count];
    memcpy(g_edit_lines, u->lines, sizeof(g_edit_lines));
    g_edit_nlines = u->nlines;
    g_edit_cx = u->cx;
    g_edit_cy = u->cy;
    g_edit_dirty = 1;
    editor_ensure_visible();
    editor_title();
}

static void editor_load(const char *path) {
    strcpy(g_edit_path, path);
    for (int i = 0; i < EDIT_LINES; i++)
        g_edit_lines[i][0] = 0;
    g_edit_nlines = 0;
    g_edit_cx = g_edit_cy = 0;
    g_edit_scroll = 0;
    g_edit_dirty = 0;
    g_undo_count = 0;

    int fd = open(path, 0, 0);
    if (fd >= 0) {
        static char buf[8192];
        long n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        int line = 0, col = 0;
        for (long i = 0; i < n && line < EDIT_LINES; i++) {
            char c = buf[i];
            if (c == '\r')
                continue;
            if (c == '\n') {
                g_edit_lines[line][col] = 0;
                line++;
                col = 0;
                continue;
            }
            if (c == '\t') {
                for (int t = 0; t < 4 && col < EDIT_COLS - 1; t++)
                    g_edit_lines[line][col++] = ' ';
                continue;
            }
            if (col < EDIT_COLS - 1)
                g_edit_lines[line][col++] = c;
        }
        if (line < EDIT_LINES && col > 0) {
            g_edit_lines[line][col] = 0;
            line++;
        }
        g_edit_nlines = line > 0 ? line : 1;
    }
    if (g_edit_nlines == 0) {
        g_edit_nlines = 1;
        g_edit_lines[0][0] = 0;
    }
    editor_title();
}

static void editor_save(void) {
    static char out[8192];
    size_t n = 0;
    for (int i = 0; i < g_edit_nlines; i++) {
        const char *ln = g_edit_lines[i];
        while (*ln && n + 2 < sizeof(out))
            out[n++] = *ln++;
        if (n + 2 < sizeof(out))
            out[n++] = '\n';
    }
    unlink(g_edit_path);
    int fd = open(g_edit_path, 0x242, 0644);
    if (fd < 0)
        return;
    write(fd, out, n);
    close(fd);
    g_edit_dirty = 0;
    editor_title();
}

static void editor_ensure_visible(void) {
    int rows = editor_rows();
    if (g_edit_cy < g_edit_scroll)
        g_edit_scroll = g_edit_cy;
    if (g_edit_cy >= g_edit_scroll + rows)
        g_edit_scroll = g_edit_cy - rows + 1;
    if (g_edit_scroll < 0)
        g_edit_scroll = 0;
}

static void editor_key(char c) {
    char *cur = g_edit_lines[g_edit_cy];
    int len = (int)strlen(cur);
    if (c == 0x13) {
        editor_save();
        return;
    }
    if (c == 0x1a) {
        editor_undo();
        return;
    }
    if (c == 8 || c == 127) {
        if (g_edit_cx > 0 || g_edit_cy > 0)
            editor_snapshot();
        if (g_edit_cx > 0) {
            for (int i = g_edit_cx - 1; i < len; i++)
                cur[i] = cur[i + 1];
            g_edit_cx--;
            g_edit_dirty = 1;
        } else if (g_edit_cy > 0) {
            char *prev = g_edit_lines[g_edit_cy - 1];
            int plen = (int)strlen(prev);
            if (plen + len < EDIT_COLS) {
                strcat(prev, cur);
                for (int i = g_edit_cy; i < g_edit_nlines - 1; i++)
                    strcpy(g_edit_lines[i], g_edit_lines[i + 1]);
                g_edit_nlines--;
                g_edit_cy--;
                g_edit_cx = plen;
                g_edit_dirty = 1;
            }
        }
    } else if (c == '\n' || c == '\r') {
        if (g_edit_nlines < EDIT_LINES) {
            editor_snapshot();
            for (int i = g_edit_nlines; i > g_edit_cy + 1; i--)
                strcpy(g_edit_lines[i], g_edit_lines[i - 1]);
            strcpy(g_edit_lines[g_edit_cy + 1], cur + g_edit_cx);
            cur[g_edit_cx] = 0;
            g_edit_nlines++;
            g_edit_cy++;
            g_edit_cx = 0;
            g_edit_dirty = 1;
        }
    } else if ((unsigned char)c == 0x80) {
        if (g_edit_cy > 0) g_edit_cy--;
        int l = (int)strlen(g_edit_lines[g_edit_cy]);
        if (g_edit_cx > l) g_edit_cx = l;
    } else if ((unsigned char)c == 0x81) {
        if (g_edit_cy + 1 < g_edit_nlines) g_edit_cy++;
        int l = (int)strlen(g_edit_lines[g_edit_cy]);
        if (g_edit_cx > l) g_edit_cx = l;
    } else if ((unsigned char)c == 0x84) {
        if (g_edit_cx > 0) g_edit_cx--;
    } else if ((unsigned char)c == 0x85) {
        if (g_edit_cx < len) g_edit_cx++;
    } else if ((unsigned char)c == 0x82) {
        g_edit_scroll -= editor_rows();
        if (g_edit_scroll < 0) g_edit_scroll = 0;
    } else if ((unsigned char)c == 0x83) {
        g_edit_scroll += editor_rows();
        int maxs = g_edit_nlines - editor_rows();
        if (maxs < 0) maxs = 0;
        if (g_edit_scroll > maxs) g_edit_scroll = maxs;
    } else if (c >= 32 && c < 127) {
        if (len < EDIT_COLS - 1) {
            editor_snapshot();
            for (int i = len; i >= g_edit_cx; i--)
                cur[i + 1] = cur[i];
            cur[g_edit_cx] = c;
            g_edit_cx++;
            g_edit_dirty = 1;
        }
    }
    editor_ensure_visible();
    editor_title();
}

static void draw_editor(int x, int y, int w, int h) {
    int rows = (h - 16) / LINE_H;
    if (rows < 1) return;
    for (int r = 0; r < rows; r++) {
        int line = g_edit_scroll + r;
        if (line >= g_edit_nlines)
            break;
        text(x + 12, y + 8 + r * LINE_H, g_edit_lines[line], 0xe5e7eb, 1);
    }
    if (g_active == EDIT_WIN) {
        int r = g_edit_cy - g_edit_scroll;
        if (r >= 0 && r < rows) {
            int l = (int)strlen(g_edit_lines[g_edit_cy]);
            int cx = g_edit_cx;
            if (cx > l) cx = l;
            fill(x + 12 + cx * 8, y + 8 + r * LINE_H + 8, 8, 2, 0x60a5fa);
        }
    }
    if (g_edit_dirty)
        text(x + w - 18, y + 6, "*", 0xfbbf24, 1);
    text(x + 12, y + h - 10, "Ctrl+S save  Ctrl+Z undo  arrows move", 0x64748b, 1);
}

static void editor_open(const char *path) {
    editor_load(path);
    struct window *w = &g_wins[EDIT_WIN];
    w->kind = W_EDIT;
    w->x = 620;
    w->y = 110;
    w->w = 620;
    w->h = 420;
    w->title = g_edit_title;
    w->visible = 1;
    raise_window(EDIT_WIN);
}

static void draw_content(int idx, int x, int y, int w, int h) {
    if (idx == 0) {
        text(x + 20, y + 20, "M10: window manager", 0xcbd5e1, 1);
        text(x + 20, y + 44, "drag bars, resize corners", 0xcbd5e1, 1);
        text(x + 20, y + 68, "taskbar: focus / new terminal", 0xcbd5e1, 1);
        fill(x + 20, y + 100, w - 60, 2, 0x334155);
        text(x + 20, y + 118, "Terminal windows run /bin/sh", 0x94a3b8, 1);
        text(x + 20, y + 140, "over pipes; type in the", 0x94a3b8, 1);
        text(x + 20, y + 162, "focused window.", 0x94a3b8, 1);
        fill(x + 20, y + 196, 140, 34, 0x2563eb);
        frame(x + 20, y + 196, 140, 34, 0x60a5fa);
        text(x + 42, y + 207, "OK", 0xffffff, 2);
        return;
    }
    if (idx == 1) {
        text(x + 18, y + 16, "kernel:  ProotOS M10", 0xa7f3d0, 1);
        text(x + 18, y + 38, "cpu:     4 x x86_64", 0xa7f3d0, 1);
        text(x + 18, y + 60, "sched:   EEVDF", 0xa7f3d0, 1);
        text(x + 18, y + 82, "input:   PS/2 aux mouse", 0xa7f3d0, 1);
        text(x + 18, y + 104, "gui:     z-order + resize", 0xa7f3d0, 1);
        text(x + 18, y + 126, "tls:     mbedTLS + CA", 0xa7f3d0, 1);
        return;
    }
    text(x + 18, y + 16, "- click raises a window", 0xfde68a, 1);
    text(x + 18, y + 38, "- drag the title bar", 0xfde68a, 1);
    text(x + 18, y + 60, "- drag the lower-right", 0xfde68a, 1);
    text(x + 18, y + 82, "  corner to resize", 0xfde68a, 1);
    text(x + 18, y + 104, "- click +Term to open", 0xfde68a, 1);
    (void)h;
}

static void draw_window(int idx) {
    struct window *w = &g_wins[idx];
    if (!w->visible)
        return;
    int active = idx == g_active;
    struct clip saved = g_clip;
    int x0 = w->x, y0 = w->y, x1 = w->x + w->w, y1 = w->y + w->h;
    if (g_clip.on) {
        if (x0 < g_clip.x) x0 = g_clip.x;
        if (y0 < g_clip.y) y0 = g_clip.y;
        if (x1 > g_clip.x + g_clip.w) x1 = g_clip.x + g_clip.w;
        if (y1 > g_clip.y + g_clip.h) y1 = g_clip.y + g_clip.h;
    }
    if (x0 >= x1 || y0 >= y1)
        return;
    g_clip.on = 1;
    g_clip.x = x0;
    g_clip.y = y0;
    g_clip.w = x1 - x0;
    g_clip.h = y1 - y0;

    fill(w->x + 8, w->y + 8, w->w, w->h, 0x05070c);
    fill(w->x, w->y, w->w, w->h, 0x111827);
    frame(w->x, w->y, w->w, w->h, active ? 0x3b82f6 : 0x475569);
    fill(w->x + 2, w->y + 2, w->w - 4, TITLE_H - 2, active ? 0x1d4ed8 : 0x334155);
    text(w->x + 14, w->y + 11, w->title, 0xffffff, 1);
    fill(w->x + w->w - 26, w->y + 8, 18, 18, 0xef4444);
    text(w->x + w->w - 22, w->y + 10, "x", 0xffffff, 1);
    if (w->kind == W_TERM) {
        if (g_terms[idx].used)
            draw_terminal(idx, w->x, w->y + TITLE_H, w->w, w->h - TITLE_H);
    } else if (w->kind == W_FILES) {
        draw_files(w->x, w->y + TITLE_H, w->w, w->h - TITLE_H);
    } else if (w->kind == W_EDIT) {
        draw_editor(w->x, w->y + TITLE_H, w->w, w->h - TITLE_H);
    } else {
        draw_content(idx, w->x, w->y + TITLE_H, w->w, w->h - TITLE_H);
    }
    /* resize grip */
    for (int i = 0; i < 12; i++)
        fill(w->x + w->w - 14 + i / 2, w->y + w->h - 3 - i / 2, 2, 2, 0x64748b);
    g_clip = saved;
}

static void taskbar(void) {
    fill(0, 0, (int)g_info.width, TASKBAR_H, 0x111827);
    fill(0, TASKBAR_H, (int)g_info.width, 2, 0x2563eb);
    text(16, 12, "ProotOS M10", 0xe5e7eb, 2);
    for (int i = 0; i < MAX_WINS; i++) {
        struct window *w = &g_wins[i];
        if (w->kind == W_TERM && !g_terms[i].used)
            continue;
        int bx = 230 + i * 116;
        uint32_t col = w->visible ? (i == g_active ? 0x1d4ed8 : 0x1f2937) : 0x111827;
        fill(bx, 7, 108, 24, col);
        frame(bx, 7, 108, 24, w->visible ? 0x3b82f6 : 0x374151);
        char label[13];
        int n = 0;
        while (w->title[n] && n < 12) { label[n] = w->title[n]; n++; }
        label[n] = 0;
        text(bx + 6, 15, label, w->visible ? 0xe5e7eb : 0x6b7280, 1);
    }
    fill(230 + MAX_WINS * 116, 7, 140, 24, 0x065f46);
    frame(230 + MAX_WINS * 116, 7, 140, 24, 0x10b981);
    text(230 + MAX_WINS * 116 + 8, 15, "+Terminal", 0xd1fae5, 1);
    fill(230 + MAX_WINS * 116 + 144, 7, 88, 24, 0x7c2d12);
    frame(230 + MAX_WINS * 116 + 144, 7, 88, 24, 0xea580c);
    text(230 + MAX_WINS * 116 + 152, 15, "+Editor", 0xffedd5, 1);
}

static void blit_back(int x, int y, int w, int h) {
    for (int j = 0; j < h; j++) {
        uint32_t *src = &g_back[(uint64_t)(y + j) * (uint32_t)g_info.width + x];
        uint8_t *dst = (uint8_t *)g_fb + (uint64_t)(y + j) * g_info.pitch + (uint64_t)x * 4;
        memcpy(dst, src, (size_t)w * 4);
    }
}

static void draw_scene_rect(int x, int y, int w, int h) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (int)g_info.width) x1 = (int)g_info.width;
    if (y1 > (int)g_info.height) y1 = (int)g_info.height;
    if (x >= x1 || y >= y1)
        return;
    g_clip.on = 1;
    g_clip.x = x;
    g_clip.y = y;
    g_clip.w = x1 - x;
    g_clip.h = y1 - y;
    if (g_back_ok) {
        g_draw = g_back;
        g_draw_stride = g_info.width;
    }
    desktop();
    taskbar();
    for (int i = 0; i < MAX_WINS; i++)
        if (g_wins[g_order[i]].visible)
            draw_window(g_order[i]);
    if (g_back_ok) {
        blit_back(x, y, x1 - x, y1 - y);
        g_draw = g_fb;
        g_draw_stride = g_info.pitch / 4;
    }
    g_clip.on = 0;
}

static void damage_add(int x, int y, int w, int h) {
    int x1 = x + w, y1 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x1 > (int)g_info.width) x1 = (int)g_info.width;
    if (y1 > (int)g_info.height) y1 = (int)g_info.height;
    if (x >= x1 || y >= y1)
        return;
    if (!g_damage_on) {
        g_damage.x = x;
        g_damage.y = y;
        g_damage.w = x1 - x;
        g_damage.h = y1 - y;
        g_damage_on = 1;
        return;
    }
    int dx1 = g_damage.x + g_damage.w, dy1 = g_damage.y + g_damage.h;
    if (x < g_damage.x) g_damage.x = x;
    if (y < g_damage.y) g_damage.y = y;
    if (x1 > dx1) dx1 = x1;
    if (y1 > dy1) dy1 = y1;
    g_damage.w = dx1 - g_damage.x;
    g_damage.h = dy1 - g_damage.y;
}

static const unsigned char cursor_bits[18][12] = {
    {1,0,0,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {1,0,1,0,0,0,0,0,0,0,0,0},
    {1,0,0,1,0,0,0,0,0,0,0,0},
    {1,0,0,0,1,0,0,0,0,0,0,0},
    {1,0,0,0,0,1,0,0,0,0,0,0},
    {1,0,0,0,0,0,1,0,0,0,0,0},
    {1,0,0,0,0,0,0,1,0,0,0,0},
    {1,0,0,0,0,0,0,0,1,0,0,0},
    {1,0,0,0,0,0,0,0,0,1,0,0},
    {1,0,0,0,0,0,0,0,0,0,1,0},
    {1,0,0,0,0,1,1,1,1,1,1,1},
    {1,0,0,0,1,0,0,0,0,0,0,0},
    {1,0,0,1,0,0,0,0,0,0,0,0},
    {1,0,1,0,0,0,0,0,0,0,0,0},
    {1,1,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0},
};

#define CUR_W 12
#define CUR_H 16

static void save_under(int x, int y, uint32_t *buf) {
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++)
            buf[j * CUR_W + i] = get(x + i, y + j);
}
static void restore_under(int x, int y, const uint32_t *buf) {
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++)
            g_fb[(uint64_t)(y + j) * (g_info.pitch / 4) + (uint32_t)(x + i)] = buf[j * CUR_W + i];
}
static void draw_cursor(int x, int y) {
    struct clip saved = g_clip;
    g_clip.on = 0;
    g_draw = g_fb;
    g_draw_stride = g_info.pitch / 4;
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++) {
            if (!cursor_bits[j][i])
                continue;
            int edge = (i == 0 || j == 0 || i == CUR_W - 1 || !cursor_bits[j][i + 1] ||
                        !cursor_bits[j][i - 1] || j == CUR_H - 1 || !cursor_bits[j + 1][i] ||
                        !cursor_bits[j - 1][i]);
            put(x + i, y + j, edge ? 0xffffff : 0x000000);
        }
    g_clip = saved;
}

static void focus_top(void) {
    for (int i = MAX_WINS - 1; i >= 0; i--) {
        if (g_wins[g_order[i]].visible) {
            g_active = g_order[i];
            return;
        }
    }
    g_active = -1;
}

static void raise_window(int idx) {
    int pos = -1;
    for (int i = 0; i < MAX_WINS; i++)
        if (g_order[i] == idx)
            pos = i;
    if (pos < 0)
        return;
    for (int i = pos; i < MAX_WINS - 1; i++)
        g_order[i] = g_order[i + 1];
    g_order[MAX_WINS - 1] = idx;
    g_active = idx;
}

static int hit_window(int x, int y) {
    for (int i = MAX_WINS - 1; i >= 0; i--) {
        int idx = g_order[i];
        struct window *w = &g_wins[idx];
        if (!w->visible)
            continue;
        if (x >= w->x && x < w->x + w->w && y >= w->y && y < w->y + w->h)
            return idx;
    }
    return -1;
}

static int spawn_terminal(int x, int y, int w, int h) {
    int slot = -1;
    for (int i = 0; i < MAX_WINS; i++)
        if (g_wins[i].kind == W_TERM && !g_terms[i].used) {
            slot = i;
            break;
        }
    if (slot < 0)
        return -1;

    int in_p[2], out_p[2];
    if (pipe(in_p) != 0 || pipe(out_p) != 0)
        return -1;

    int had_fb = g_fb != 0;
    if (had_fb)
        fb_unmap();
    int pid = fork();
    if (pid == 0) {
        dup2(in_p[0], 0);
        dup2(out_p[1], 1);
        dup2(out_p[1], 2);
        close(in_p[0]);
        close(in_p[1]);
        close(out_p[0]);
        close(out_p[1]);
        for (int fd = 3; fd < 16; fd++)
            close(fd); /* drop inherited terminals, mouse and keyboard */
        char *sargv[] = {"sh", "stdin", 0};
        execve("/bin/sh", sargv, 0);
        _exit(1);
    }
    if (had_fb) {
        g_fb = fb_map();
        if (!g_fb)
            return -1;
    }
    if (pid < 0) {
        close(in_p[0]);
        close(in_p[1]);
        close(out_p[0]);
        close(out_p[1]);
        return -1;
    }
    close(in_p[0]);
    close(out_p[1]);
    term_init(slot);
    g_terms[slot].in_fd = in_p[1];
    g_terms[slot].out_fd = out_p[0];
    g_terms[slot].pid = pid;
    fcntl(g_terms[slot].out_fd, F_SETFL, O_NONBLOCK);

    struct window *win = &g_wins[slot];
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->title = "Terminal";
    win->kind = W_TERM;
    win->visible = 1;
    raise_window(slot);
    return slot;
}

static void close_terminal(int idx) {
    struct term_state *t = &g_terms[idx];
    if (t->in_fd >= 0)
        close(t->in_fd);
    if (t->out_fd >= 0)
        close(t->out_fd);
    t->in_fd = t->out_fd = -1;
    t->used = 0;
    g_wins[idx].visible = 0;
    focus_top();
}

static int g_drag = -1;
static int g_drag_dx, g_drag_dy;
static int g_resize = -1;

static void handle_press(int x, int y) {
    if (y < TASKBAR_H) {
        if (x >= 230 + MAX_WINS * 116 + 144 && x < 230 + MAX_WINS * 116 + 232) {
            editor_open("/untitled");
            return;
        }
        if (x >= 230 + MAX_WINS * 116 && x < 230 + MAX_WINS * 116 + 140) {
            int n = 0;
            for (int i = 0; i < MAX_WINS; i++)
                if (g_wins[i].kind == W_TERM && g_terms[i].used)
                    n++;
            int sx = 300 + (n % 3) * 40;
            int sy = 160 + (n % 3) * 30;
            spawn_terminal(sx, sy, 680, 360);
            return;
        }
        int idx = (x - 230) / 116;
        if (x >= 230 && idx >= 0 && idx < MAX_WINS && x - (230 + idx * 116) < 108) {
            struct window *w = &g_wins[idx];
            if (w->kind == W_TERM && !g_terms[idx].used)
                return;
            w->visible = 1;
            raise_window(idx);
        }
        return;
    }
    int idx = hit_window(x, y);
    if (idx < 0)
        return;
    raise_window(idx);
    struct window *w = &g_wins[idx];
    if (x >= w->x + w->w - 26 && x < w->x + w->w - 8 &&
        y >= w->y + 8 && y < w->y + 26) {
        if (w->kind == W_TERM)
            close_terminal(idx);
        else {
            w->visible = 0;
            focus_top();
        }
        return;
    }
    if (x >= w->x + w->w - 18 && y >= w->y + w->h - 18) {
        g_resize = idx;
        return;
    }
    if (w->kind == W_FILES) {
        files_click(x, y, w->x, w->y + TITLE_H, w->w, w->h - TITLE_H);
        return;
    }
    if (w->kind == W_TERM && y > w->y + TITLE_H) {
        struct term_state *t = &g_terms[idx];
        int ly = y - (w->y + TITLE_H + 8);
        int r = ly / LINE_H;
        if (ly >= 0 && r >= 0 && r < t->disp_count) {
            int line = t->disp_line[r];
            t->sel_a = t->sel_b = line;
            g_sel_win = idx;
        }
        return;
    }
    if (w->kind == W_EDIT && y > w->y + TITLE_H) {
        int lx = x - (w->x + 12);
        int ly = y - (w->y + TITLE_H + 8);
        if (lx >= 0 && ly >= 0) {
            int rows = (w->h - TITLE_H - 16) / LINE_H;
            if (rows < 1) rows = 1;
            int cy = g_edit_scroll + ly / LINE_H;
            if (cy >= g_edit_nlines) cy = g_edit_nlines - 1;
            int cx = lx / 8;
            int len = (int)strlen(g_edit_lines[cy]);
            if (cx > len) cx = len;
            g_edit_cy = cy;
            g_edit_cx = cx;
        }
        return;
    }
    if (y < w->y + TITLE_H) {
        g_drag = idx;
        g_drag_dx = x - w->x;
        g_drag_dy = y - w->y;
    }
}

static int arg_int(const char *s) {
    int v = 0;
    while (*s >= '0' && *s <= '9')
        v = v * 10 + (*s++ - '0');
    return v;
}

int main(int argc, char **argv) {
    if (fb_info(&g_info) != 0) {
        printf("gui: no framebuffer\n");
        return 1;
    }
    g_rgb = g_info.format == 0;
    int seconds = argc > 1 ? arg_int(argv[1]) : 0;

    g_fb = fb_map();
    if (!g_fb) {
        printf("gui: fb_map failed\n");
        return 1;
    }
    g_draw = g_fb;
    g_draw_stride = g_info.pitch / 4;
    g_back_ok = g_info.width <= BACK_MAX_W && g_info.height <= BACK_MAX_H;
    printf("gui: framebuffer %ux%u %ubpp pitch=%u\n", g_info.width, g_info.height,
           g_info.bpp, g_info.pitch);
    console_gui(1); /* keep kernel logs off the GUI framebuffer */

    files_refresh();
    spawn_terminal(300, 160, 680, 360);
    draw_scene_rect(0, 0, (int)g_info.width, (int)g_info.height);
    int fb_ok = get(740, 460) == rgba(0x111827);

    int mfd = open("/dev/mouse", 0, 0);
    int kfd = open("/dev/kbd", 0, 0);
    if (mfd < 0)
        printf("gui: no /dev/mouse\n");
    if (kfd < 0)
        printf("gui: no /dev/kbd\n");

    int cx = (int)g_info.width / 2;
    int cy = (int)g_info.height / 2;
    uint32_t under[CUR_W * CUR_H];
    save_under(cx, cy, under);
    draw_cursor(cx, cy);

    int events = 0, frames = 0;
    int limit = seconds > 0 ? seconds * 50 : -1;
    int buttons = 0;

    while (limit < 0 || frames < limit) {
        struct mouse_event ev;
        int moved = 0;
        int ox = cx, oy = cy;
        if (mfd >= 0) {
            while (read(mfd, &ev, sizeof(ev)) == (int)sizeof(ev)) {
                int prev_buttons = buttons;
                buttons = ev.buttons & 1;
                cx += ev.dx;
                cy += ev.dy;
                if (cx < 0) cx = 0;
                if (cy < 0) cy = 0;
                if (cx > (int)g_info.width - CUR_W) cx = (int)g_info.width - CUR_W;
                if (cy > (int)g_info.height - CUR_H) cy = (int)g_info.height - CUR_H;
                moved = 1;
                events++;
                if (buttons && !prev_buttons) {
                    handle_press(cx, cy);
                    damage_add(0, 0, (int)g_info.width, (int)g_info.height);
                }
                if (!buttons && prev_buttons) {
                    g_drag = -1;
                    g_resize = -1;
                    g_sel_win = -1;
                }
                if (ev.wheel && g_active >= 0 && g_wins[g_active].kind == W_TERM) {
                    struct term_state *t = &g_terms[g_active];
                    struct window *tw = &g_wins[g_active];
                    t->offset += ev.wheel * 3;
                    if (t->offset > t->hist_count) t->offset = t->hist_count;
                    if (t->offset < 0) t->offset = 0;
                    damage_add(tw->x - 8, tw->y - 8, tw->w + 16, tw->h + 16);
                }
                if (g_drag >= 0 && (ev.dx || ev.dy)) {
                    struct window *w = &g_wins[g_drag];
                    damage_add(w->x - 8, w->y - 8, w->w + 16, w->h + 16);
                    w->x = cx - g_drag_dx;
                    w->y = cy - g_drag_dy;
                    if (w->x < 0) w->x = 0;
                    if (w->y < TASKBAR_H) w->y = TASKBAR_H;
                    if (w->x + w->w > (int)g_info.width) w->x = (int)g_info.width - w->w;
                    if (w->y + w->h > (int)g_info.height) w->y = (int)g_info.height - w->h;
                    damage_add(w->x - 8, w->y - 8, w->w + 16, w->h + 16);
                }
                if (g_sel_win >= 0 && buttons) {
                    struct window *sw = &g_wins[g_sel_win];
                    struct term_state *st = &g_terms[g_sel_win];
                    int ly = cy - (sw->y + TITLE_H + 8);
                    int r = ly / LINE_H;
                    if (ly < 0) r = 0;
                    if (r >= st->disp_count) r = st->disp_count - 1;
                    if (r >= 0 && st->disp_count > 0) {
                        st->sel_b = st->disp_line[r];
                        damage_add(sw->x - 8, sw->y - 8, sw->w + 16, sw->h + 16);
                    }
                }
                if (g_resize >= 0 && (ev.dx || ev.dy)) {
                    struct window *w = &g_wins[g_resize];
                    damage_add(w->x - 8, w->y - 8, w->w + 16, w->h + 16);
                    w->w = cx - w->x + 2;
                    w->h = cy - w->y + 2;
                    if (w->w < MIN_W) w->w = MIN_W;
                    if (w->h < MIN_H) w->h = MIN_H;
                    if (w->x + w->w > (int)g_info.width) w->w = (int)g_info.width - w->x;
                    if (w->y + w->h > (int)g_info.height) w->h = (int)g_info.height - w->y;
                    damage_add(w->x - 8, w->y - 8, w->w + 16, w->h + 16);
                }
            }
        }

        for (int i = 0; i < MAX_WINS; i++) {
            struct term_state *t = &g_terms[i];
            if (g_wins[i].kind != W_TERM || !t->used || t->out_fd < 0)
                continue;
            char tb[256];
            long tn;
            while ((tn = read(t->out_fd, tb, sizeof(tb))) > 0) {
                term_feed(i, tb, (int)tn);
                damage_add(g_wins[i].x - 8, g_wins[i].y - 8, g_wins[i].w + 16,
                           g_wins[i].h + 16);
            }
        }

        char c;
        int quit = 0;
        if (kfd >= 0) {
            while (read(kfd, &c, 1) == 1) {
                if ((unsigned char)c == 27) {
                    quit = 1;
                    break;
                }
                if (g_active >= 0 && g_wins[g_active].kind == W_FILES) {
                    struct window *fw = &g_wins[g_active];
                    if ((unsigned char)c == 0x80) {
                        if (g_files_sel > 0) g_files_sel--;
                        else if (g_files_sel < 0 && g_files_count > 0) g_files_sel = 0;
                    } else if ((unsigned char)c == 0x81) {
                        if (g_files_sel < g_files_count - 1) g_files_sel++;
                    } else if (c == '\n' || c == '\r') {
                        if (g_files_sel >= 0) files_select(g_files_sel);
                    } else if (c == 8 || c == 127) {
                        files_up();
                    } else if (c == 'e' || c == 'E') {
                        if (g_files_selpath[0]) editor_open(g_files_selpath);
                    }
                    damage_add(fw->x - 8, fw->y - 8, fw->w + 16, fw->h + 16);
                    continue;
                }
                if (g_active >= 0 && g_wins[g_active].kind == W_EDIT) {
                    if ((unsigned char)c == 0x16) {
                        for (int i = 0; i < g_clipboard_len; i++)
                            editor_key(g_clipboard[i]);
                    } else {
                        editor_key(c);
                    }
                    struct window *ew = &g_wins[g_active];
                    damage_add(ew->x - 8, ew->y - 8, ew->w + 16, ew->h + 16);
                    continue;
                }
                if (g_active >= 0 && g_wins[g_active].kind == W_TERM) {
                    struct term_state *t = &g_terms[g_active];
                    struct window *tw = &g_wins[g_active];
                    if ((unsigned char)c == 0x03) {
                        terminal_copy(g_active);
                        damage_add(tw->x - 8, tw->y - 8, tw->w + 16, tw->h + 16);
                        continue;
                    }
                    if ((unsigned char)c == 0x16) {
                        if (g_clipboard_len > 0 && t->used && t->in_fd >= 0)
                            write(t->in_fd, g_clipboard, (size_t)g_clipboard_len);
                        continue;
                    }
                    int rows = (tw->h - TITLE_H - 16) / LINE_H;
                    if (rows > TERM_ROWS) rows = TERM_ROWS;
                    if (rows < 1) rows = 1;
                    if ((unsigned char)c == 0x82) {
                        t->offset += rows;
                        if (t->offset > t->hist_count) t->offset = t->hist_count;
                        damage_add(tw->x - 8, tw->y - 8, tw->w + 16, tw->h + 16);
                        continue;
                    }
                    if ((unsigned char)c == 0x83) {
                        t->offset -= rows;
                        if (t->offset < 0) t->offset = 0;
                        damage_add(tw->x - 8, tw->y - 8, tw->w + 16, tw->h + 16);
                        continue;
                    }
                    if (t->used && t->in_fd >= 0) {
                        t->offset = 0;
                        t->sel_a = t->sel_b = -1;
                        write(t->in_fd, &c, 1);
                    }
                }
            }
        }
        if (quit)
            break;

        if (g_damage_on) {
            damage_add(ox, oy, CUR_W, CUR_H);
            damage_add(cx, cy, CUR_W, CUR_H);
            draw_scene_rect(g_damage.x, g_damage.y, g_damage.w, g_damage.h);
            g_damage_on = 0;
            save_under(cx, cy, under);
            draw_cursor(cx, cy);
        } else if (moved) {
            restore_under(ox, oy, under);
            save_under(cx, cy, under);
            draw_cursor(cx, cy);
        }
        frames++;
        sleep_ms(20);
    }

    restore_under(cx, cy, under);
    console_gui(0);
    for (int i = 0; i < MAX_WINS; i++)
        if (g_wins[i].kind == W_TERM)
            close_terminal(i);
    if (mfd >= 0)
        close(mfd);
    if (kfd >= 0)
        close(kfd);
    printf("gui: framebuffer check %s\n", fb_ok ? "PASS" : "FAIL");
    printf("gui: terminal shell %s (%d bytes)\n", g_term_total > 0 ? "PASS" : "FAIL",
           g_term_total);
    printf("gui: desktop drawn, %d frames, %d mouse events\n", frames, events);
    return 0;
}
