#pragma once
#include "kernel.h"
void net_init(void);
int net_ping(void);
int net_ping_ip(const uint8_t ip[4]);
int net_http_get(const char *host, const char *path, char *out, size_t cap);
int net_dns_query(const char *name, uint8_t out_ip[4]);
