#pragma once
#include "kernel.h"
void net_init(void);
int net_ping(void);
int net_ping_ip(const uint8_t ip[4]);
int net_http_get(const char *host, const char *path, char *out, size_t cap);
int net_https_get(const char *host, const char *path, char *out, size_t cap);
int tcp_connect(const uint8_t ip[4], uint16_t port);
int tcp_conn_write(const void *data, size_t len);
int tcp_conn_read(void *data, size_t len);
void tcp_conn_close(void);
int net_dns_query(const char *name, uint8_t out_ip[4]);
