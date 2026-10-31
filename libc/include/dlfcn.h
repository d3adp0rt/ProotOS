#pragma once
#define RTLD_LAZY 1
#define RTLD_GLOBAL 0x100
#define RTLD_DEFAULT ((void *)0)
void *dlopen(const char *name, int flags);
void *dlsym(void *h, const char *name);
int dlclose(void *h);
char *dlerror(void);
