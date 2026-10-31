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
static uint8_t g_ip[4] = { 10, 0, 2, 15 };
static uint8_t g_gw[4] = { 10, 0, 2, 2 };
static uint8_t g_dns[4] = { 10, 0, 2, 3 };
static uint8_t g_gw_mac[6];
static uint32_t g_dhcp_xid;

static uint16_t be16(uint16_t v) { return (uint16_t)((v << 8) | (v >> 8)); }
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static uint32_t rd32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

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

#define DHCP_MAGIC 0x63825363u
#define BOOTP_LEN 300

static uint16_t dhcp_rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static int dhcp_send(uint8_t type, const uint8_t *req_ip, const uint8_t *server_id) {
    uint8_t frame[14 + 20 + 8 + BOOTP_LEN];
    memset(frame, 0, sizeof(frame));
    memset(frame, 0xff, 6);
    memcpy(frame + 6, g_mac, 6);
    put16(frame + 12, ETH_IP);

    uint8_t *ip = frame + 14;
    ip[0] = 0x45;
    ip[8] = 64;
    ip[9] = 17;
    put16(ip + 2, (uint16_t)(20 + 8 + BOOTP_LEN));
    put16(ip + 4, (uint16_t)(0x4444 + type));
    /* 0.0.0.0 -> 255.255.255.255 */
    memset(ip + 12, 0, 4);
    memset(ip + 16, 0xff, 4);
    put16(ip + 10, checksum(ip, 20));

    uint8_t *udp = ip + 20;
    put16(udp, 68);
    put16(udp + 2, 67);
    put16(udp + 4, (uint16_t)(8 + BOOTP_LEN));
    put16(udp + 6, 0); /* checksum optional for IPv4 */

    uint8_t *b = udp + 8;
    b[0] = 1;  /* BOOTREQUEST */
    b[1] = 1;  /* ethernet */
    b[2] = 6;  /* hlen */
    put32(b + 4, g_dhcp_xid);
    put16(b + 10, 0x8000); /* broadcast reply */
    memcpy(b + 28, g_mac, 6);
    put32(b + 236, DHCP_MAGIC);

    uint8_t *o = b + 240;
    *o++ = 53; *o++ = 1; *o++ = type;
    *o++ = 55; *o++ = 4; *o++ = 1; *o++ = 3; *o++ = 6; *o++ = 15;
    *o++ = 61; *o++ = 7; *o++ = 1; memcpy(o, g_mac, 6); o += 6;
    *o++ = 12; *o++ = 7; memcpy(o, "prootos", 7); o += 7;
    if (type == 3 && req_ip) {
        *o++ = 50; *o++ = 4; memcpy(o, req_ip, 4); o += 4;
    }
    if (type == 3 && server_id) {
        *o++ = 54; *o++ = 4; memcpy(o, server_id, 4); o += 4;
    }
    *o++ = 255;
    return rtl8139_send(frame, sizeof(frame));
}

static int dhcp_recv(uint8_t *msg_type, uint8_t *yiaddr, uint8_t *server_id,
                     uint8_t *subnet, uint8_t *router, uint8_t *dns, int timeout_ms) {
    uint8_t rx[1600];
    for (int i = 0; i < timeout_ms; i++) {
        size_t n = 0;
        if (rtl8139_poll(rx, sizeof(rx), &n) > 0 && n >= 42 && rx[12] == 0x08 && rx[13] == 0x00) {
            uint8_t *ip = rx + 14;
            if (ip[9] != 17)
                goto next;
            {
                uint8_t *udp = ip + 20;
                if (dhcp_rd16(udp) != 67 || dhcp_rd16(udp + 2) != 68)
                    goto next;
                uint8_t *b = udp + 8;
                if (rd32(b + 4) != g_dhcp_xid || rd32(b + 236) != DHCP_MAGIC)
                    goto next;
                memset(yiaddr, 0, 4);
                memset(server_id, 0, 4);
                memset(subnet, 0, 4);
                memset(router, 0, 4);
                memset(dns, 0, 4);
                *msg_type = 0;
                memcpy(yiaddr, b + 16, 4);
                uint8_t *o = b + 240;
                while (o < b + BOOTP_LEN) {
                    uint8_t code = *o++;
                    if (code == 0)
                        continue;
                    if (code == 255)
                        break;
                    if (o >= b + BOOTP_LEN)
                        break;
                    uint8_t len = *o++;
                    if (o + len > b + BOOTP_LEN)
                        break;
                    if (code == 53 && len >= 1) *msg_type = o[0];
                    else if (code == 54 && len == 4) memcpy(server_id, o, 4);
                    else if (code == 1 && len == 4) memcpy(subnet, o, 4);
                    else if (code == 3 && len >= 4) memcpy(router, o, 4);
                    else if (code == 6 && len >= 4) memcpy(dns, o, 4);
                    o += len;
                }
                return 0;
            }
        }
next:
        timer_sleep_ms(1);
    }
    return -1;
}

int net_dhcp(void) {
    uint8_t msg_type, yiaddr[4], server_id[4], subnet[4], router[4], dns[4];
    g_dhcp_xid = 0x39030000u ^ timer_ticks() ^ ((uint32_t)g_mac[3] << 16) ^
                 ((uint32_t)g_mac[4] << 8) ^ g_mac[5];

    int offered = 0;
    for (int attempt = 0; attempt < 3 && !offered; attempt++) {
        if (dhcp_send(1, 0, 0) != 0)
            return -1;
        if (dhcp_recv(&msg_type, yiaddr, server_id, subnet, router, dns, 3000) == 0 &&
            msg_type == 2)
            offered = 1;
    }
    if (!offered) {
        kprintf("[net] DHCP: no OFFER, keeping static config\n");
        return -1;
    }
    kprintf("[net] DHCP offer %u.%u.%u.%u from %u.%u.%u.%u\n",
            yiaddr[0], yiaddr[1], yiaddr[2], yiaddr[3], server_id[0], server_id[1],
            server_id[2], server_id[3]);

    for (int attempt = 0; attempt < 3; attempt++) {
        if (dhcp_send(3, yiaddr, server_id) != 0)
            return -1;
        if (dhcp_recv(&msg_type, yiaddr, server_id, subnet, router, dns, 3000) != 0)
            continue;
        if (msg_type == 5) {
            memcpy(g_ip, yiaddr, 4);
            if (router[0] || router[1] || router[2] || router[3])
                memcpy(g_gw, router, 4);
            else {
                g_gw[0] = g_ip[0]; g_gw[1] = g_ip[1]; g_gw[2] = g_ip[2]; g_gw[3] = 2;
            }
            if (dns[0] || dns[1] || dns[2] || dns[3])
                memcpy(g_dns, dns, 4);
            else
                memcpy(g_dns, g_gw, 4);
            kprintf("[net] DHCP lease %u.%u.%u.%u gw %u.%u.%u.%u dns %u.%u.%u.%u\n",
                    g_ip[0], g_ip[1], g_ip[2], g_ip[3], g_gw[0], g_gw[1], g_gw[2],
                    g_gw[3], g_dns[0], g_dns[1], g_dns[2], g_dns[3]);
            return 0;
        }
        if (msg_type == 6) {
            kprintf("[net] DHCP NAK\n");
            return -1;
        }
    }
    kprintf("[net] DHCP: no ACK, keeping static config\n");
    return -1;
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

int net_dns_query(const char *name, uint8_t out_ip[4]) {
    uint8_t frame[256];
    memset(frame, 0, sizeof(frame));
    memcpy(frame, g_gw_mac, 6);
    memcpy(frame + 6, g_mac, 6);
    put16(frame + 12, ETH_IP);
    uint8_t *ip = frame + 14;
    ip[0] = 0x45;
    ip[8] = 64;
    ip[9] = 17;
    memcpy(ip + 12, g_ip, 4);
    memcpy(ip + 16, g_dns, 4);
    uint8_t *udp = ip + 20;
    put16(udp, 12345);
    put16(udp + 2, 53);
    uint8_t *dns = udp + 8;
    put16(dns, 0x1234);
    put16(dns + 2, 0x0100);
    put16(dns + 4, 1);
    uint8_t *q = dns + 12;
    const char *p = name;
    while (*p) {
        const char *dot = p;
        while (*dot && *dot != '.') dot++;
        size_t n = (size_t)(dot - p);
        *q++ = (uint8_t)n;
        memcpy(q, p, n);
        q += n;
        p = *dot ? dot + 1 : dot;
    }
    *q++ = 0;
    put16(q, 1); q += 2;
    put16(q, 1); q += 2;
    put16(udp + 4, (uint16_t)(8 + (q - dns)));
    put16(ip + 2, (uint16_t)(20 + 8 + (q - dns)));
    put16(ip + 10, checksum(ip, 20));
    if (rtl8139_send(frame, 14 + 20 + 8 + (size_t)(q - dns)) != 0) return -1;

    uint8_t rx[1600];
    for (int i = 0; i < 1000; i++) {
        size_t n = 0;
        if (rtl8139_poll(rx, sizeof(rx), &n) > 0 && n >= 42 && rx[12] == 0x08 && rx[13] == 0x00) {
            uint8_t *rip = rx + 14;
            if (rip[9] == 17) {
                uint8_t *rudp = rip + 20;
                uint8_t *rdns = rudp + 8;
                if (rdns[0] == 0x12 && rdns[1] == 0x34 && (rdns[3] & 0x0f) == 0) {
                    uint16_t qd = (uint16_t)((rdns[4] << 8) | rdns[5]);
                    uint16_t an = (uint16_t)((rdns[6] << 8) | rdns[7]);
                    uint8_t *ap = rdns + 12;
                    for (int k = 0; k < qd; k++) {
                        while (*ap) ap += *ap + 1;
                        ap += 5;
                    }
                    for (int k = 0; k < an; k++) {
                        if ((*ap & 0xc0) == 0xc0) ap += 2;
                        else { while (*ap) ap += *ap + 1; ap++; }
                        uint16_t type = (uint16_t)((ap[0] << 8) | ap[1]);
                        uint16_t rdlen = (uint16_t)((ap[8] << 8) | ap[9]);
                        ap += 10;
                        if (type == 1 && rdlen == 4) {
                            memcpy(out_ip, ap, 4);
                            kprintf("[net] DNS %s -> %u.%u.%u.%u\n", name, out_ip[0], out_ip[1],
                                    out_ip[2], out_ip[3]);
                            return 0;
                        }
                        ap += rdlen;
                    }
                }
            }
        }
        timer_sleep_ms(1);
    }
    kprintf("[net] DNS timeout for %s\n", name);
    return -1;
}


#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

static uint16_t tcp_checksum(const uint8_t *src, const uint8_t *dst, const uint8_t *tcp,
                             size_t len) {
    uint32_t sum = 0;
    for (int i = 0; i < 4; i += 2) sum += (uint16_t)((src[i] << 8) | src[i + 1]);
    for (int i = 0; i < 4; i += 2) sum += (uint16_t)((dst[i] << 8) | dst[i + 1]);
    sum += 6 + (uint32_t)len;
    for (size_t i = 0; i + 1 < len; i += 2) sum += (uint16_t)((tcp[i] << 8) | tcp[i + 1]);
    if (len & 1) sum += (uint16_t)(tcp[len - 1] << 8);
    while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}

static int tcp_send(const uint8_t *dst_mac, const uint8_t *dst_ip, uint16_t sport, uint16_t dport,
                    uint32_t seq, uint32_t ack, uint8_t flags, const void *data, size_t len) {
    uint8_t frame[1500];
    memset(frame, 0, sizeof(frame));
    memcpy(frame, dst_mac, 6);
    memcpy(frame + 6, g_mac, 6);
    put16(frame + 12, ETH_IP);
    uint8_t *ip = frame + 14;
    ip[0] = 0x45; ip[8] = 64; ip[9] = 6;
    put16(ip + 2, (uint16_t)(20 + 20 + len));
    put16(ip + 4, 0x6000);
    memcpy(ip + 12, g_ip, 4);
    memcpy(ip + 16, dst_ip, 4);
    put16(ip + 10, checksum(ip, 20));
    uint8_t *tcp = ip + 20;
    put16(tcp + 0, sport); put16(tcp + 2, dport);
    put32(tcp + 4, seq); put32(tcp + 8, ack);
    tcp[12] = 5 << 4; tcp[13] = flags;
    put16(tcp + 14, 8192);
    if (len) memcpy(tcp + 20, data, len);
    uint16_t tc = tcp_checksum(g_ip, dst_ip, tcp, 20 + len);
    put16(tcp + 16, tc ? tc : 0xffff);
    return rtl8139_send(frame, 14 + 20 + 20 + len);
}

static int tcp_recv(uint8_t *out, size_t cap, size_t *out_len, uint16_t sport, uint16_t dport,
                    uint8_t *flags, uint32_t *seq, uint32_t *ack, int timeout_ms) {
    uint8_t rx[1600];
    for (int i = 0; i < timeout_ms; i++) {
        size_t n = 0;
        if (rtl8139_poll(rx, sizeof(rx), &n) > 0 && n >= 54 && rx[12] == 0x08 && rx[13] == 0x00) {
            uint8_t *ip = rx + 14;
            if (ip[9] == 6) {
                uint8_t *tcp = ip + 20;
                uint16_t sp = (uint16_t)((tcp[0] << 8) | tcp[1]);
                uint16_t dp = (uint16_t)((tcp[2] << 8) | tcp[3]);
                if (sp == sport && dp == dport) {
                    uint8_t off = (uint8_t)((tcp[12] >> 4) * 4);
                    size_t plen = (size_t)((ip[2] << 8) | ip[3]) - 20 - off;
                    *flags = tcp[13];
                    *seq = rd32(tcp + 4);
                    *ack = rd32(tcp + 8);
                    if (out_len) *out_len = plen > cap ? cap : plen;
                    if (out && plen) memcpy(out, tcp + off, plen > cap ? cap : plen);
                    return 0;
                }
            }
        }
        timer_sleep_ms(1);
    }
    return -1;
}


struct tcp_sock {
    uint8_t ip[4];
    uint16_t sport, dport;
    uint32_t seq, ack;
    int open;
};
static struct tcp_sock g_sock;
static uint16_t g_sock_port = 42000;
static uint8_t g_tcp_buf[4096];
static size_t g_tcp_len, g_tcp_pos;

int tcp_connect(const uint8_t ip[4], uint16_t port) {
    g_sock.ip[0]=ip[0]; g_sock.ip[1]=ip[1]; g_sock.ip[2]=ip[2]; g_sock.ip[3]=ip[3];
    g_sock.sport = g_sock_port++;
    if (g_sock_port > 49000) g_sock_port = 42000;
    g_sock.dport = port;
    g_sock.seq = 0x2000;
    g_sock.ack = 0;
    g_sock.open = 0;
    g_tcp_len = g_tcp_pos = 0;
    if (tcp_send(g_gw_mac, ip, g_sock.sport, port, g_sock.seq, 0, TCP_SYN, 0, 0) != 0) return -1;
    uint8_t flags; uint32_t rseq, rack; size_t n;
    if (tcp_recv(0, 0, &n, port, g_sock.sport, &flags, &rseq, &rack, 3000) != 0) return -1;
    if (!(flags & TCP_SYN) || !(flags & TCP_ACK) || rack != g_sock.seq + 1) return -1;
    g_sock.ack = rseq + 1;
    g_sock.seq++;
    tcp_send(g_gw_mac, ip, g_sock.sport, port, g_sock.seq, g_sock.ack, TCP_ACK, 0, 0);
    g_sock.open = 1;
    return 0;
}

int tcp_conn_write(const void *data, size_t len) {
    if (!g_sock.open) return -1;
    if (tcp_send(g_gw_mac, g_sock.ip, g_sock.sport, g_sock.dport, g_sock.seq, g_sock.ack,
                 TCP_PSH | TCP_ACK, data, len) != 0) return -1;
    g_sock.seq += (uint32_t)len;
    return (int)len;
}

int tcp_conn_read(void *data, size_t len) {
    if (!g_sock.open) return -1;
    if (g_tcp_pos < g_tcp_len) {
        size_t n = g_tcp_len - g_tcp_pos;
        if (n > len) n = len;
        memcpy(data, g_tcp_buf + g_tcp_pos, n);
        g_tcp_pos += n;
        return (int)n;
    }
    uint8_t flags; uint32_t rseq, rack; size_t n = 0;
    for (int i = 0; i < 6000; i++) {
        if (tcp_recv(g_tcp_buf, sizeof(g_tcp_buf), &n, g_sock.dport, g_sock.sport, &flags, &rseq,
                     &rack, 1) == 0) {
            if (flags & TCP_RST) { g_sock.open = 0; return -1; }
            if (n) {
                /* Drop duplicates and out-of-order segments; the peer will
                 * retransmit. Accepting them corrupts the TLS byte stream. */
                if (rseq != g_sock.ack) {
                    tcp_send(g_gw_mac, g_sock.ip, g_sock.sport, g_sock.dport, g_sock.seq,
                             g_sock.ack, TCP_ACK, 0, 0);
                    continue;
                }
                g_sock.ack = rseq + (uint32_t)n;
                g_tcp_len = n;
                g_tcp_pos = 0;
                tcp_send(g_gw_mac, g_sock.ip, g_sock.sport, g_sock.dport, g_sock.seq, g_sock.ack,
                         TCP_ACK, 0, 0);
                size_t out = n;
                if (out > len) out = len;
                memcpy(data, g_tcp_buf, out);
                g_tcp_pos = out;
                return (int)out;
            }
            if (flags & TCP_FIN) {
                if (rseq == g_sock.ack) {
                    g_sock.ack++;
                    tcp_send(g_gw_mac, g_sock.ip, g_sock.sport, g_sock.dport, g_sock.seq,
                             g_sock.ack, TCP_ACK, 0, 0);
                }
                g_sock.open = 0;
                return 0;
            }
        }
    }
    return -1;
}

void tcp_conn_close(void) {
    if (!g_sock.open) return;
    tcp_send(g_gw_mac, g_sock.ip, g_sock.sport, g_sock.dport, g_sock.seq, g_sock.ack,
             TCP_FIN | TCP_ACK, 0, 0);
    g_sock.open = 0;
}

int net_ping_ip(const uint8_t ip[4]) {
    uint8_t frame[128];
    memset(frame, 0, sizeof(frame));
    memcpy(frame, g_gw_mac, 6);
    memcpy(frame + 6, g_mac, 6);
    put16(frame + 12, ETH_IP);
    uint8_t *p = frame + 14;
    p[0] = 0x45; p[8] = 64; p[9] = IP_ICMP;
    put16(p + 2, 32); put16(p + 4, 0x1235);
    memcpy(p + 12, g_ip, 4); memcpy(p + 16, ip, 4);
    put16(p + 10, checksum(p, 20));
    uint8_t *icmp = p + 20;
    icmp[0] = 8; put16(icmp + 4, 0x1235); put16(icmp + 6, 1);
    icmp[8]='P'; icmp[9]='N'; icmp[10]='G'; icmp[11]='!';
    put16(icmp + 2, checksum(icmp, 12));
    if (rtl8139_send(frame, 46) != 0) return -1;
    uint8_t rx[1600];
    for (int i = 0; i < 1000; i++) {
        size_t n = 0;
        if (rtl8139_poll(rx, sizeof(rx), &n) > 0 && n >= 42 && rx[12] == 0x08 && rx[13] == 0x00) {
            uint8_t *rip = rx + 14;
            if (rip[9] == 1 && rip[20] == 0 && memcmp(rip + 12, ip, 4) == 0) return 0;
        }
        timer_sleep_ms(1);
    }
    return -1;
}

static uint16_t g_next_port = 40000;

int net_http_get(const char *host, const char *path, char *out, size_t cap) {
    uint8_t ip[4];
    if (net_dns_query(host, ip) != 0) return -1;
    uint16_t sport = g_next_port++, dport = 80;
    if (g_next_port >= 50000) g_next_port = 40000;
    uint32_t seq = 0x1000;
    uint8_t flags;
    uint32_t rseq, rack;
    size_t n = 0;

    if (tcp_send(g_gw_mac, ip, sport, dport, seq, 0, TCP_SYN, 0, 0) != 0) return -1;
    if (tcp_recv(0, 0, &n, dport, sport, &flags, &rseq, &rack, 2000) != 0) return -1;
    if (!(flags & TCP_SYN) || !(flags & TCP_ACK) || rack != seq + 1) return -1;
    uint32_t server_seq = rseq;
    seq++;
    tcp_send(g_gw_mac, ip, sport, dport, seq, server_seq + 1, TCP_ACK, 0, 0);

    char req[256];
    int rl = ksnprintf(req, sizeof(req),
                       "GET %s HTTP/1.0\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    if (tcp_send(g_gw_mac, ip, sport, dport, seq, server_seq + 1, TCP_PSH | TCP_ACK, req,
                 (size_t)rl) != 0) return -1;
    seq += (uint32_t)rl;
    uint32_t expected = server_seq + 1;

    size_t total = 0;
    for (int i = 0; i < 5000; i++) {
        if (tcp_recv((uint8_t *)out + total, cap > total ? cap - total : 0, &n, dport, sport,
                     &flags, &rseq, &rack, 1) == 0) {
            if (flags & TCP_RST) break;
            if (n) {
                total += n;
                expected = rseq + (uint32_t)n;
                tcp_send(g_gw_mac, ip, sport, dport, seq, expected, TCP_ACK, 0, 0);
            }
            if (flags & TCP_FIN) {
                tcp_send(g_gw_mac, ip, sport, dport, seq, expected + 1, TCP_ACK, 0, 0);
                break;
            }
            if (total >= cap) break;
        }
    }
    if (!total) return -1;
    char *body = 0;
    for (size_t i = 0; i + 3 < total; i++) {
        if (out[i] == '\r' && out[i+1] == '\n' && out[i+2] == '\r' && out[i+3] == '\n') {
            body = out + i + 4;
            break;
        }
    }
    if (!body) return -1;
    if (total >= 12 && out[0] == 'H') {
        int code = (out[9] - '0') * 100 + (out[10] - '0') * 10 + (out[11] - '0');
        if (code >= 300 && code < 400) {
            for (size_t i = 0; i + 8 < total; i++) {
                if ((out[i] == 'L' || out[i] == 'l') &&
                    strncmp(out + i, "Location:", 9) == 0) {
                    char *loc = out + i + 9;
                    while (*loc == ' ') loc++;
                    if (strncmp(loc, "https://", 8) == 0) return -2;
                    break;
                }
            }
        }
    }
    size_t blen = total - (size_t)(body - out);
    memmove(out, body, blen);
    return (int)blen;
}

int net_ping(void) {
    if (arp_resolve() != 0) {
        kprintf("[net] ARP timeout, using SLIRP gateway MAC\n");
        memcpy(g_gw_mac, "\x52\x55\x0a\x00\x02\x02", 6);
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
