#include <unistd.h>
#include <fcntl.h>
#include "py/mpconfig.h"

int sleep_ms(unsigned ms);

/*
 * Core UART functions to implement for a port
 */

#if MICROPY_MIN_USE_STM32_MCU
typedef struct {
    volatile uint32_t SR;
    volatile uint32_t DR;
} periph_uart_t;
#define USART1 ((periph_uart_t *)0x40011000)
#endif

// Receive single character
int mp_hal_stdin_rx_chr(void) {
    unsigned char c = 0;
    #if MICROPY_MIN_USE_STDOUT
    static int infd = -1;
    static char pending[8];
    static int plen, pidx;
    if (pidx < plen)
        return (unsigned char)pending[pidx++];
    if (infd < 0) {
        infd = 0;
        if (fcntl(0, F_GETFL, 0) < 0)
            infd = open("/dev/kbd", 0, 0);
    }
    for (;;) {
        int r = read(infd, &c, 1);
        if (r == 1)
            break;
        if (r < 0)
            return 0;
        sleep_ms(10);
    }
    /* Translate ProotOS key codes into the VT100 sequences readline expects. */
    const char *seq = 0;
    switch (c) {
    case '\n': c = '\r'; break;
    case 0x80: seq = "\x1b[A"; break;   /* up */
    case 0x81: seq = "\x1b[B"; break;   /* down */
    case 0x84: seq = "\x1b[D"; break;   /* left */
    case 0x85: seq = "\x1b[C"; break;   /* right */
    case 0x82: seq = "\x1b[5~"; break;  /* page up */
    case 0x83: seq = "\x1b[6~"; break;  /* page down */
    default: break;
    }
    if (seq) {
        plen = 0;
        while (seq[plen] && plen < (int)sizeof(pending))
            pending[plen] = seq[plen], plen++;
        pidx = 1;
        return (unsigned char)pending[0];
    }
    #elif MICROPY_MIN_USE_STM32_MCU
    // wait for RXNE
    while ((USART1->SR & (1 << 5)) == 0) {
    }
    c = USART1->DR;
    #endif
    return c;
}

// Send string of given length
mp_uint_t mp_hal_stdout_tx_strn(const char *str, mp_uint_t len) {
    mp_uint_t ret = len;
    #if MICROPY_MIN_USE_STDOUT
    int r = write(STDOUT_FILENO, str, len);
    if (r >= 0) {
        // in case of an error in the syscall, report no bytes written
        ret = 0;
    }
    #elif MICROPY_MIN_USE_STM32_MCU
    while (len--) {
        // wait for TXE
        while ((USART1->SR & (1 << 7)) == 0) {
        }
        USART1->DR = *str++;
    }
    #endif
    return ret;
}
