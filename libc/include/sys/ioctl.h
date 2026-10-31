#pragma once
struct winsize {
    unsigned short ws_row, ws_col, ws_xpixel, ws_ypixel;
};
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
int ioctl(int fd, unsigned long request, ...);
