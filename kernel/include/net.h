#pragma once
#include "kernel.h"
void net_init(void);
int net_ping(void);
int net_dns_query(const char *name, uint8_t out_ip[4]);
