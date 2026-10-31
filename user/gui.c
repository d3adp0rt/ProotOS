/*
 * ProotOS — M10a userspace desktop demo.
 *
 * Maps the GOP framebuffer through SYS_FB_MAP, draws a small desktop and
 * tracks the PS/2 mouse through /dev/mouse. Exits on ESC or after ~6 s.
 */
#include "libc.h"
#include "../third_party/fonts/font8x8_basic.h"

static uint32_t *g_fb;
static struct fb_info g_info;
static int g_rgb;

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

    fill(0, 0, (int)g_info.width, 38, 0x111827);
    fill(0, 38, (int)g_info.width, 2, 0x2563eb);
    text(16, 12, "ProotOS M10", 0xe5e7eb, 2);
    text((int)g_info.width - 210, 14, "userspace compositor", 0x93c5fd, 1);

    fill(150, 130, 640, 380, 0x000000); /* shadow */
    fill(140, 120, 640, 380, 0x111827);
    frame(140, 120, 640, 380, 0x3b82f6);
    fill(142, 122, 636, 34, 0x1d4ed8);
    text(154, 131, "ProotOS Desktop", 0xffffff, 1);
    fill(748, 128, 20, 20, 0xef4444);
    text(754, 130, "x", 0xffffff, 1);

    text(170, 190, "M10a: kernel framebuffer mapped into user space", 0xcbd5e1, 1);
    text(170, 215, "PS/2 aux mouse -> /dev/mouse", 0xcbd5e1, 1);
    text(170, 240, "1280x720 GOP desktop, 8x8 font, 2x scale", 0xcbd5e1, 1);
    fill(170, 280, 580, 2, 0x334155);
    text(170, 300, "This window is drawn by /bin/gui with plain pixels.", 0x94a3b8, 1);
    text(170, 322, "No toolkit, no X11, no Wayland: int 0x80 + mmap.", 0x94a3b8, 1);

    fill(170, 370, 140, 34, 0x2563eb);
    frame(170, 370, 140, 34, 0x60a5fa);
    text(192, 381, "OK", 0xffffff, 2);

    fill(820, 300, 330, 210, 0x111827);
    frame(820, 300, 330, 210, 0x10b981);
    fill(822, 302, 326, 28, 0x065f46);
    text(834, 309, "System", 0xecfdf5, 1);
    text(838, 344, "kernel:  ProotOS", 0xa7f3d0, 1);
    text(838, 366, "cpu:     4 x x86_64", 0xa7f3d0, 1);
    text(838, 388, "sched:   EEVDF", 0xa7f3d0, 1);
    text(838, 410, "tls:     mbedTLS 1.2", 0xa7f3d0, 1);
    text(838, 432, "pkg:     ppkg + registry", 0xa7f3d0, 1);
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

int main(void) {
    if (fb_info(&g_info) != 0) {
        printf("gui: no framebuffer\n");
        return 1;
    }
    g_fb = fb_map();
    if (!g_fb) {
        printf("gui: fb_map failed\n");
        return 1;
    }
    g_rgb = g_info.format == 0;
    printf("gui: framebuffer %ux%u %ubpp pitch=%u\n", g_info.width, g_info.height,
           g_info.bpp, g_info.pitch);

    desktop();
    int fb_ok = get(200, 180) == rgba(0x111827);

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

    int events = 0;
    int frames = 0;
    for (; frames < 500; frames++) {
        struct mouse_event ev;
        int moved = 0;
        int ox = cx, oy = cy;
        if (mfd >= 0) {
            while (read(mfd, &ev, sizeof(ev)) == (int)sizeof(ev)) {
                cx += ev.dx;
                cy += ev.dy;
                if (cx < 0) cx = 0;
                if (cy < 0) cy = 0;
                if (cx > (int)g_info.width - CUR_W) cx = (int)g_info.width - CUR_W;
                if (cy > (int)g_info.height - CUR_H) cy = (int)g_info.height - CUR_H;
                moved = 1;
                events++;
            }
        }
        char c;
        if (kfd >= 0 && read(kfd, &c, 1) == 1 && c == 27)
            break;
        if (moved) {
            restore_under(ox, oy, under);
            save_under(cx, cy, under);
            draw_cursor(cx, cy);
        }
        sleep_ms(20);
    }

    restore_under(cx, cy, under);
    if (mfd >= 0)
        close(mfd);
    if (kfd >= 0)
        close(kfd);
    printf("gui: framebuffer check %s\n", fb_ok ? "PASS" : "FAIL");
    printf("gui: desktop drawn, %d frames, %d mouse events\n", frames, events);
    return 0;
}
