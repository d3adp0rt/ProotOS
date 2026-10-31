/*
 * ProotOS — framebuffer console with VGA text fallback.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "kernel.h"
#include "spinlock.h"
#include "../third_party/fonts/font8x8_basic.h"

#define VGA_TEXT ((volatile uint16_t *)0xb8000)
#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_ATTR 0x1f /* white on blue */

static struct {
    uint8_t *fb;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint64_t phys;
    uint64_t size;
    uint32_t format;
    int vga;
    int scale;
    uint32_t char_w;
    uint32_t char_h;
    uint32_t cursor_x;
    uint32_t cursor_y;
    uint32_t fg;
    uint32_t bg;
} con;

static spinlock_t g_console_lock = SPINLOCK_INIT;

static uint32_t pack_color(uint32_t rgb) {
    if (con.format == PROOT_FB_FORMAT_RGB) {
        uint32_t r = (rgb >> 16) & 0xff;
        uint32_t g = (rgb >> 8) & 0xff;
        uint32_t b = rgb & 0xff;
        return r | (g << 8) | (b << 16);
    }
    /* BGR (and default): memory bytes are B, G, R, X. */
    return rgb & 0x00ffffff;
}

static void fb_putpixel(uint32_t x, uint32_t y, uint32_t rgb) {
    if (!con.fb || x >= con.width || y >= con.height)
        return;
    uint32_t bpp = con.bpp / 8;
    if (bpp != 4)
        bpp = 4;
    *(uint32_t *)(con.fb + (uint64_t)y * con.pitch + (uint64_t)x * bpp) = pack_color(rgb);
}

static void fb_fill(uint32_t rgb) {
    uint32_t v = pack_color(rgb);
    uint32_t bpp = con.bpp / 8;
    if (bpp != 4)
        bpp = 4;
    for (uint32_t y = 0; y < con.height; y++) {
        uint32_t *row = (uint32_t *)(con.fb + (uint64_t)y * con.pitch);
        for (uint32_t x = 0; x < con.width; x++)
            row[x] = v;
    }
}

static void vga_clear(void) {
    for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
        VGA_TEXT[i] = (uint16_t)(' ' | (VGA_ATTR << 8));
}

static void vga_scroll(void) {
    memmove((void *)VGA_TEXT, (void *)(VGA_TEXT + VGA_COLS),
            (VGA_ROWS - 1) * VGA_COLS * sizeof(uint16_t));
    for (int i = 0; i < VGA_COLS; i++) {
        VGA_TEXT[(VGA_ROWS - 1) * VGA_COLS + i] = (uint16_t)(' ' | (VGA_ATTR << 8));
    }
}

static void vga_putc(char c) {
    if (c == '\n') {
        con.cursor_x = 0;
        con.cursor_y++;
    } else if (c == '\r') {
        con.cursor_x = 0;
    } else if (c == '\b') {
        if (con.cursor_x)
            con.cursor_x--;
        VGA_TEXT[con.cursor_y * VGA_COLS + con.cursor_x] = (uint16_t)(' ' | (VGA_ATTR << 8));
    } else if (c == '\t') {
        for (int i = 0; i < 4; i++)
            vga_putc(' ');
    } else {
        VGA_TEXT[con.cursor_y * VGA_COLS + con.cursor_x] =
            (uint16_t)((unsigned char)c | (VGA_ATTR << 8));
        con.cursor_x++;
        if (con.cursor_x >= VGA_COLS) {
            con.cursor_x = 0;
            con.cursor_y++;
        }
    }
    if (con.cursor_y >= VGA_ROWS) {
        vga_scroll();
        con.cursor_y = VGA_ROWS - 1;
    }
}

static void fb_scroll(void) {
    uint32_t row_bytes = con.char_h * con.pitch;
    memmove(con.fb, con.fb + row_bytes, (uint64_t)(con.height - con.char_h) * con.pitch);
    for (uint32_t y = con.height - con.char_h; y < con.height; y++) {
        uint32_t *row = (uint32_t *)(con.fb + (uint64_t)y * con.pitch);
        uint32_t v = pack_color(con.bg);
        for (uint32_t x = 0; x < con.width; x++)
            row[x] = v;
    }
}

static void fb_newline(void) {
    con.cursor_x = 0;
    con.cursor_y += con.char_h;
    if (con.cursor_y + con.char_h > con.height) {
        fb_scroll();
        con.cursor_y = con.height - con.char_h;
    }
}

static void fb_putc(char c) {
    if (c == '\n') {
        fb_newline();
        return;
    }
    if (c == '\r') {
        con.cursor_x = 0;
        return;
    }
    if (c == '\b') {
        if (con.cursor_x >= con.char_w)
            con.cursor_x -= con.char_w;
        return;
    }
    if (c == '\t') {
        for (int i = 0; i < 4; i++)
            fb_putc(' ');
        return;
    }
    if (con.cursor_x + con.char_w > con.width)
        fb_newline();

    unsigned char ch = (unsigned char)c;
    if (ch < 32 || ch >= 127)
        ch = '?';
    /* Clear the whole cell first so space/backspace really erases. */
    for (uint32_t yy = 0; yy < con.char_h; yy++) {
        for (uint32_t xx = 0; xx < con.char_w; xx++) {
            fb_putpixel(con.cursor_x + xx, con.cursor_y + yy, con.bg);
        }
    }
    const unsigned char *glyph = (const unsigned char *)font8x8_basic[ch];
    for (uint32_t row = 0; row < 8; row++) {
        for (uint32_t col = 0; col < 8; col++) {
            if (glyph[row] & (1u << col)) {
                for (int sy = 0; sy < con.scale; sy++) {
                    for (int sx = 0; sx < con.scale; sx++) {
                        fb_putpixel(con.cursor_x + col * con.scale + sx,
                                    con.cursor_y + row * con.scale + sy, con.fg);
                    }
                }
            }
        }
    }
    con.cursor_x += con.char_w;
}

void console_init(const struct proot_bootinfo *bi) {
    memset(&con, 0, sizeof(con));
    con.fg = 0x00e6edf3; /* light gray-blue */
    con.bg = 0x000b1220; /* very dark blue */

    if (bi && bi->framebuffer_base && bi->framebuffer_width && bi->framebuffer_height) {
        con.fb = (uint8_t *)(uintptr_t)bi->framebuffer_base;
        con.phys = bi->framebuffer_phys;
        con.size = bi->framebuffer_size;
        con.width = bi->framebuffer_width;
        con.height = bi->framebuffer_height;
        con.pitch = bi->framebuffer_pitch;
        con.bpp = bi->framebuffer_bpp;
        con.format = bi->framebuffer_format;
        con.scale = (con.width >= 1024 && con.height >= 600) ? 2 : 1;
        con.char_w = 8 * con.scale;
        con.char_h = 8 * con.scale;
        fb_fill(con.bg);
    } else {
        con.vga = 1;
        con.scale = 1;
        con.char_w = 8;
        con.char_h = 16;
        vga_clear();
    }
}

void console_fb_info(struct fb_info *out) {
    if (!out)
        return;
    memset(out, 0, sizeof(*out));
    if (!con.fb)
        return;
    out->width = con.width;
    out->height = con.height;
    out->pitch = con.pitch;
    out->bpp = con.bpp;
    out->format = con.format;
}

uint64_t console_fb_phys(void) { return con.phys; }

uint64_t console_fb_size(void) {
    if (!con.fb)
        return 0;
    return (uint64_t)con.pitch * con.height;
}

void console_clear(void) {
    if (con.fb) {
        fb_fill(con.bg);
        con.cursor_x = 0;
        con.cursor_y = 0;
    } else if (con.vga) {
        vga_clear();
        con.cursor_x = 0;
        con.cursor_y = 0;
    }
}

void console_putc(char c) {
    serial_putc(c);
    if (con.fb)
        fb_putc(c);
    else if (con.vga)
        vga_putc(c);
}

void console_write(const char *s) {
    uint64_t flags = spin_lock_irqsave(&g_console_lock);
    while (*s)
        console_putc(*s++);
    spin_unlock_irqrestore(&g_console_lock, flags);
}
