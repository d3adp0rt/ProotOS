/*
 * ProotOS — minimal ARP/IPv4/ICMP client over RTL8139 (M9 core).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "net.h"
#include "rtl8139.h"
#include "timer.h"

#define ETH_ARP 0x0806
#define ETH_IP 0x0800
#define IP_ICMP 1

static uint8_t g_mac[6];
static const uint8_t g_ip[4] = { 10, 0, 2, 15 };
static const uint8_t g_gw[4] = { 10, 0, 2, 2 };
static uint8_t g_gw_mac[6];

static uint16_t be16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static uint16_t checksum(const void *data, size_t len) {
    const uint16_t *p = data;
    uint32_t sum = 0;
    while (len > 1) { sum += be16(*p++); len -= 2; }
    if (len) sum += (uint16_t)((*(const uint8_t *)p) << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

void net_init(void) {
    const uint8_t *mac = rtl8139_mac();
    if (mac) memcpy(g_mac, mac, 6);
}

static int arp_resolve(void) {
    uint8_t frame[64];
    memset(frame, 0xff, 6);
    memcpy(frame + 6, g_mac, 6);
    put16(frame + 12, ETH_ARP);
    put16(frame + 14, 1);
    put16(frame + 16, ETH_IP);
    frame[18] = 6;
    frame[19] = 4;
    put16(frame + 20, 1);
    memcpy(frame + 22, g_mac, 6);
    memcpy(frame + 28, g_ip, 4);
    memset(frame + 32, 0, 6);
    memcpy(frame + 38, g_gw, 4);
    if (rtl8139_send(frame, 42) != 0) return -1;

    uint8_t rx[1600];
    for (int i = 0; i < 500; i++) {
        size_t n = 0;
        if (rtl8139_poll(rx, sizeof(rx), &n) > 0 && n >= 42 && rx[12] == 0x08 && rx[13] == 0x06) {
            if (memcmp(rx + 38, g_ip, 4) == 0) {
                memcpy(g_gw_mac, rx + 22, 6);
                return 0;
            }
        }
        timer_sleep_ms(1);
    }
    return -1;
}

int net_ping(void) {
    if (arp_resolve() != 0) {
        kprintf("[net] ARP timeout\n");
        return -1;
    }
    kprintf("[net] gateway MAC %02x:%02x:%02x:%02x:%02x:%02x\n", g_gw_mac[0], g_gw_mac[1],
            g_gw_mac[2], g_gw_mac[3], g_gw_mac[4], g_gw_mac[5]);

    uint8_t frame[128];
    memset(frame, 0, sizeof(frame));
    memcpy(frame, g_gw_mac, 6);
    memcpy(frame + 6, g_mac, 6);
    put16(frame + 12, ETH_IP);
    uint8_t *ip = frame + 14;
    ip[0] = 0x45;
    ip[8] = 64;
    ip[9] = IP_ICMP;
    put16(ip + 2, 32);
    put16(ip + 4, 0x1234);
    memcpy(ip + 12, g_ip, 4);
    memcpy(ip + 16, g_gw, 4);
    put16(ip + 10, checksum(ip, 20));
    uint8_t *icmp = ip + 20;
    icmp[0] = 8;
    put16(icmp + 4, 0x1234);
    put16(icmp + 6, 1);
    icmp[8] = 'P'; icmp[9] = 'N'; icmp[10] = 'G'; icmp[11] = '!';
    put16(icmp + 2, checksum(icmp, 12));
    if (rtl8139_send(frame, 14 + 20 + 12) != 0) return -1;

    uint8_t rx[1600];
    for (int i = 0; i < 500; i++) {
        size_t n = 0;
        if (rtl8139_poll(rx, sizeof(rx), &n) > 0 && n >= 42 && rx[12] == 0x08 && rx[13] == 0x00) {
            uint8_t *rip = rx + 14;
            if (rip[9] == IP_ICMP && rip[20] == 0) {
                kprintf("[net] ICMP echo reply from 10.0.2.2\n");
                return 0;
            }
        }
        timer_sleep_ms(1);
    }
    kprintf("[net] ICMP timeout\n");
    return -1;
}
