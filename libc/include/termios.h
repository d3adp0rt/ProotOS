#pragma once
typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;
#define NCCS 32
struct termios {
    tcflag_t c_iflag, c_oflag, c_cflag, c_lflag;
    cc_t c_line;
    cc_t c_cc[NCCS];
    speed_t c_ispeed, c_ospeed;
};
#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2
#define TCOOFF 0
#define TCOON 1
#define TCIOFF 2
#define TCION 3
#define TCIFLUSH 0
#define TCOFLUSH 1
#define TCIOFLUSH 2
#define IGNBRK 1
#define BRKINT 2
#define IGNPAR 4
#define PARMRK 8
#define INPCK 16
#define ISTRIP 32
#define INLCR 64
#define IGNCR 128
#define ICRNL 256
#define IXON 512
#define OPOST 1
#define ONLCR 4
#define ISIG 1
#define ICANON 2
#define ECHO 8
#define ECHOE 16
#define ECHOK 32
#define ECHONL 64
#define NOFLSH 128
#define IEXTEN 0x8000
#define VINTR 0
#define VQUIT 1
#define VERASE 2
#define VKILL 3
#define VEOF 4
#define VTIME 5
#define VMIN 6
#define B0 0
#define B9600 13
#define B38400 15
#define CS8 0x30
#define CREAD 0x80
int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int action, const struct termios *t);
void cfmakeraw(struct termios *t);
int tcflush(int fd, int queue);
speed_t cfgetospeed(const struct termios *t);
speed_t cfgetispeed(const struct termios *t);
int cfsetospeed(struct termios *t, speed_t speed);
int cfsetispeed(struct termios *t, speed_t speed);
