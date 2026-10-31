/*
 * ProotOS — M10b userspace window manager demo.
 *
 * Maps the GOP framebuffer through SYS_FB_MAP, draws overlapping windows,
 * handles PS/2 mouse clicks, focus, z-order and title-bar dragging.
 * Usage: gui [seconds]   (default: run until ESC)
 */
#include "libc.h"
#include "../third_party/fonts/font8x8_basic.h"

#define MAX_WINS 4
#define TERM_COLS 78
#define TERM_ROWS 30
#define TITLE_H 34
#define TASKBAR_H 38

struct window {
    int x, y, w, h;
    const char *title;
    int visible;
};

static uint32_t *g_fb;
static struct fb_info g_info;
static int g_rgb;

static struct window g_wins[MAX_WINS] = {
    {140, 110, 560, 320, "ProotOS Desktop", 1},
    {420, 300, 400, 220, "System", 1},
    {900, 120, 300, 200, "Notes", 1},
    {300, 160, 680, 360, "Terminal", 1},
};
static int g_order[MAX_WINS] = {0, 1, 2, 3}; /* bottom .. top */
static int g_active = 3;

/* Terminal window (index 3) runs /bin/sh over two pipes. */
static char g_term[TERM_ROWS][TERM_COLS];
static int g_term_row, g_term_col;
static int g_term_in = -1;   /* write end -> shell stdin */
static int g_term_out = -1;  /* read end  <- shell stdout/stderr */
static int g_term_esc;
static int g_term_bytes;

static uint32_t rgba(uint32_t rgb) {
    if (g_rgb)
        return ((rgb & 0xff) << 16) | (rgb & 0xff00) | ((rgb >> 16) & 0xff);
    return rgb & 0x00ffffff;
}

static void put(int x, int y, uint32_t rgb) {
    if (x < 0 || y < 0 || (uint32_t)x >= g_info.width || (uint32_t)y >= g_info.height)
        return;
    g_fb[(uint64_t)y * (g_info.pitch / 4) + (uint32_t)x] = rgba(rgb);
}

static uint32_t get(int x, int y) {
    return g_fb[(uint64_t)y * (g_info.pitch / 4) + (uint32_t)x];
}

static void fill(int x, int y, int w, int h, uint32_t rgb) {
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            put(x + i, y + j, rgb);
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

static void desktop(void) {
    for (uint32_t y = 0; y < g_info.height; y++) {
        uint32_t t = y * 255 / g_info.height;
        uint32_t r = 10 + t / 6, g = 18 + t / 3, b = 34 + t / 2;
        for (uint32_t x = 0; x < g_info.width; x++)
            put((int)x, (int)y, (r << 16) | (g << 8) | b);
    }
}

static void term_scroll(void) {
    for (int r = 1; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++)
            g_term[r - 1][c] = g_term[r][c];
    for (int c = 0; c < TERM_COLS; c++)
        g_term[TERM_ROWS - 1][c] = ' ';
    g_term_row = TERM_ROWS - 1;
}

static void term_newline(void) {
    g_term_col = 0;
    g_term_row++;
    if (g_term_row >= TERM_ROWS)
        term_scroll();
}

static void term_putc(char ch) {
    if (g_term_esc) {
        if (g_term_esc == 1 && ch == '[') { g_term_esc = 2; return; }
        g_term_esc = 0;
        return;
    }
    if (ch == 27) { g_term_esc = 1; return; }
    if (ch == '\n') { term_newline(); return; }
    if (ch == '\r') { g_term_col = 0; return; }
    if (ch == '\b') { if (g_term_col > 0) g_term_col--; return; }
    if ((unsigned char)ch < 32) return;
    if (g_term_col >= TERM_COLS)
        term_newline();
    g_term[g_term_row][g_term_col++] = ch;
}

static void term_feed(const char *buf, int n) {
    g_term_bytes += n;
    for (int i = 0; i < n; i++)
        term_putc(buf[i]);
}

static void term_init_grid(void) {
    for (int r = 0; r < TERM_ROWS; r++)
        for (int c = 0; c < TERM_COLS; c++)
            g_term[r][c] = ' ';
    g_term_row = 0;
    g_term_col = 0;
    g_term_esc = 0;
}

static void draw_terminal(int x, int y, int w, int h) {
    (void)w;
    int cx = x + 12, cy = y + 8;
    for (int r = 0; r < TERM_ROWS && (r * 8) < h - 16; r++) {
        char line[TERM_COLS + 1];
        for (int c = 0; c < TERM_COLS; c++)
            line[c] = g_term[r][c];
        line[TERM_COLS] = 0;
        text(cx, cy + r * 8, line, 0x9becb8, 1);
    }
    if (g_active == 3 && g_term_row < TERM_ROWS)
        fill(cx + g_term_col * 8, cy + g_term_row * 8, 8, 8, 0x9becb8);
}

static void draw_content(int idx, int x, int y, int w, int h) {
    (void)w;
    if (idx == 0) {
        text(x + 20, y + 20, "M10b: userspace window manager", 0xcbd5e1, 1);
        text(x + 20, y + 44, "click a title bar to focus, drag to move", 0xcbd5e1, 1);
        text(x + 20, y + 68, "ESC exits; windows can overlap", 0xcbd5e1, 1);
        fill(x + 20, y + 100, w - 60, 2, 0x334155);
        text(x + 20, y + 118, "This window is drawn by /bin/gui with", 0x94a3b8, 1);
        text(x + 20, y + 140, "plain pixels: no toolkit, no X11.", 0x94a3b8, 1);
        fill(x + 20, y + 180, 140, 34, 0x2563eb);
        frame(x + 20, y + 180, 140, 34, 0x60a5fa);
        text(x + 42, y + 191, "OK", 0xffffff, 2);
        return;
    }
    if (idx == 1) {
        text(x + 18, y + 16, "kernel:  ProotOS M10b", 0xa7f3d0, 1);
        text(x + 18, y + 38, "cpu:     4 x x86_64", 0xa7f3d0, 1);
        text(x + 18, y + 60, "sched:   EEVDF", 0xa7f3d0, 1);
        text(x + 18, y + 82, "input:   PS/2 aux mouse", 0xa7f3d0, 1);
        text(x + 18, y + 104, "gui:     z-order + drag", 0xa7f3d0, 1);
        text(x + 18, y + 126, "tls:     mbedTLS + CA", 0xa7f3d0, 1);
        return;
    }
    text(x + 18, y + 16, "- click raises a window", 0xfde68a, 1);
    text(x + 18, y + 38, "- drag the blue title bar", 0xfde68a, 1);
    text(x + 18, y + 60, "- red X closes a window", 0xfde68a, 1);
    text(x + 18, y + 82, "- taskbar restores it", 0xfde68a, 1);
    (void)h;
}



static void draw_window(int idx) {
    struct window *w = &g_wins[idx];
    if (!w->visible)
        return;
    int active = idx == g_active;
    fill(w->x + 8, w->y + 8, w->w, w->h, 0x05070c);
    fill(w->x, w->y, w->w, w->h, 0x111827);
    frame(w->x, w->y, w->w, w->h, active ? 0x3b82f6 : 0x475569);
    fill(w->x + 2, w->y + 2, w->w - 4, TITLE_H - 2, active ? 0x1d4ed8 : 0x334155);
    text(w->x + 14, w->y + 11, w->title, 0xffffff, 1);
    fill(w->x + w->w - 26, w->y + 8, 18, 18, 0xef4444);
    text(w->x + w->w - 22, w->y + 10, "x", 0xffffff, 1);
    if (idx == 3)
        draw_terminal(w->x, w->y + TITLE_H, w->w, w->h - TITLE_H);
    else
        draw_content(idx, w->x, w->y + TITLE_H, w->w, w->h - TITLE_H);
}

static void taskbar(void) {
    fill(0, 0, (int)g_info.width, TASKBAR_H, 0x111827);
    fill(0, TASKBAR_H, (int)g_info.width, 2, 0x2563eb);
    text(16, 12, "ProotOS M10", 0xe5e7eb, 2);
    int bx = 230;
    for (int i = 0; i < MAX_WINS; i++) {
        struct window *w = &g_wins[i];
        uint32_t col = w->visible ? (i == g_active ? 0x1d4ed8 : 0x1f2937) : 0x111827;
        fill(bx, 7, 150, 24, col);
        frame(bx, 7, 150, 24, w->visible ? 0x3b82f6 : 0x374151);
        text(bx + 10, 15, w->title, w->visible ? 0xe5e7eb : 0x6b7280, 1);
        bx += 158;
    }
}

static void draw_scene(void) {
    desktop();
    taskbar();
    for (int i = 0; i < MAX_WINS; i++)
        draw_window(g_order[i]);
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
    for (int j = 0; j < CUR_H; j++)
        for (int i = 0; i < CUR_W; i++) {
            if (!cursor_bits[j][i])
                continue;
            int edge = (i == 0 || j == 0 || i == CUR_W - 1 || !cursor_bits[j][i + 1] ||
                        !cursor_bits[j][i - 1] || j == CUR_H - 1 || !cursor_bits[j + 1][i] ||
                        !cursor_bits[j - 1][i]);
            put(x + i, y + j, edge ? 0xffffff : 0x000000);
        }
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

static int g_drag = -1;
static int g_drag_dx, g_drag_dy;

static void handle_press(int x, int y) {
    if (y < TASKBAR_H) {
        int bx = 230;
        for (int i = 0; i < MAX_WINS; i++) {
            if (x >= bx && x < bx + 150) {
                g_wins[i].visible = 1;
                raise_window(i);
                return;
            }
            bx += 158;
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
        w->visible = 0;
        focus_top();
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

    /* Start the terminal shell before mapping the framebuffer: fork copies
     * the address space, and the fb mapping must not be cloned. */
    term_init_grid();
    int in_p[2], out_p[2];
    if (pipe(in_p) == 0 && pipe(out_p) == 0) {
        int pid = fork();
        if (pid == 0) {
            dup2(in_p[0], 0);
            dup2(out_p[1], 1);
            dup2(out_p[1], 2);
            close(in_p[0]);
            close(in_p[1]);
            close(out_p[0]);
            close(out_p[1]);
            char *sargv[] = {"sh", "stdin", 0};
            execve("/bin/sh", sargv, 0);
            _exit(1);
        }
        if (pid > 0) {
            close(in_p[0]);
            close(out_p[1]);
            g_term_in = in_p[1];
            g_term_out = out_p[0];
            fcntl(g_term_out, F_SETFL, O_NONBLOCK);
        }
    } else {
        printf("gui: terminal pipes failed\n");
    }

    g_fb = fb_map();
    if (!g_fb) {
        printf("gui: fb_map failed\n");
        return 1;
    }
    printf("gui: framebuffer %ux%u %ubpp pitch=%u\n", g_info.width, g_info.height,
           g_info.bpp, g_info.pitch);

    draw_scene();
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

    int events = 0, frames = 0, redraw = 0;
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
                    redraw = 1;
                }
                if (!buttons && prev_buttons)
                    g_drag = -1;
                if (g_drag >= 0 && (ev.dx || ev.dy)) {
                    struct window *w = &g_wins[g_drag];
                    w->x = cx - g_drag_dx;
                    w->y = cy - g_drag_dy;
                    if (w->x < 0) w->x = 0;
                    if (w->y < TASKBAR_H) w->y = TASKBAR_H;
                    if (w->x + w->w > (int)g_info.width) w->x = (int)g_info.width - w->w;
                    if (w->y + w->h > (int)g_info.height) w->y = (int)g_info.height - w->h;
                    redraw = 1;
                }
            }
        }
        if (g_term_out >= 0) {
            char tb[256];
            long tn;
            int got = 0;
            while ((tn = read(g_term_out, tb, sizeof(tb))) > 0) {
                term_feed(tb, (int)tn);
                got = 1;
            }
            if (got)
                redraw = 1;
        }

        char c;
        int quit = 0;
        if (kfd >= 0) {
            while (read(kfd, &c, 1) == 1) {
                if ((unsigned char)c == 27) {
                    quit = 1;
                    break;
                }
                if (g_active == 3 && g_term_in >= 0)
                    write(g_term_in, &c, 1);
            }
        }
        if (quit)
            break;

        if (redraw) {
            draw_scene();
            save_under(cx, cy, under);
            draw_cursor(cx, cy);
            redraw = 0;
        } else if (moved) {
            restore_under(ox, oy, under);
            save_under(cx, cy, under);
            draw_cursor(cx, cy);
        }
        frames++;
        sleep_ms(20);
    }

    restore_under(cx, cy, under);
    if (g_term_in >= 0)
        close(g_term_in);
    if (g_term_out >= 0)
        close(g_term_out);
    if (mfd >= 0)
        close(mfd);
    if (kfd >= 0)
        close(kfd);
    printf("gui: framebuffer check %s\n", fb_ok ? "PASS" : "FAIL");
    printf("gui: terminal shell %s (%d bytes)\n", g_term_bytes > 0 ? "PASS" : "FAIL",
           g_term_bytes);
    printf("gui: desktop drawn, %d frames, %d mouse events\n", frames, events);
    return 0;
}
