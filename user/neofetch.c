#include "libc.h"
#include <stdio.h>
#include <string.h>

static const char *logo[] = {
    "  ____            _   ___  ____",
    " |  _ \\ _ __ ___ | |_/ _ \\/ ___|",
    " | |_) | '__/ _ \\| __| | | \\___ \\",
    " |  __/| | | (_) | |_| |_| |___) |",
    " |_|   |_|  \\___/ \\__|\\___/|____/",
    "",
    "    ProotOS / UEFI x86_64",
};

static int count_lines(const char *path) {
    int fd = open(path, 0, 0);
    if (fd < 0)
        return 0;
    char c;
    int n = 0;
    while (read(fd, &c, 1) == 1)
        if (c == '\n')
            n++;
    close(fd);
    return n;
}

int main(void) {
    struct utsname u;
    struct proot_sysinfo si;
    struct fb_info fb;
    uname(&u);
    proot_sysinfo(&si);
    fb_info(&fb);

    unsigned secs = (unsigned)(si.uptime_ms / 1000);
    unsigned h = secs / 3600, m = (secs / 60) % 60, s = secs % 60;
    unsigned total_mb = (unsigned)(si.total_kb / 1024);
    unsigned free_mb = (unsigned)(si.free_kb / 1024);
    unsigned used_mb = total_mb > free_mb ? total_mb - free_mb : 0;
    int packages = count_lines("/var/lib/ppkg/index");

    char procs[2048];
    long pn = getprocs(procs, sizeof(procs));
    int processes = 0;
    if (pn > 0)
        for (long i = 0; i < pn; i++)
            if (procs[i] == '\n')
                processes++;

    char info[10][160];
    int n = 0;
    snprintf(info[n++], 160, "OS: %s %s %s", u.sysname, u.release, u.machine);
    snprintf(info[n++], 160, "Host: QEMU / UEFI");
    snprintf(info[n++], 160, "Kernel: %s %s", u.release, u.version);
    snprintf(info[n++], 160, "Uptime: %uh %um %us", h, m, s);
    snprintf(info[n++], 160, "Shell: sh");
    snprintf(info[n++], 160, "Resolution: %ux%u", fb.width, fb.height);
    snprintf(info[n++], 160, "CPU: %u x %s", si.ncpu, u.machine);
    snprintf(info[n++], 160, "Memory: %u MiB / %u MiB", used_mb, total_mb);
    snprintf(info[n++], 160, "Packages: %d", packages);
    snprintf(info[n++], 160, "Processes: %d", processes);

    int lines = (int)(sizeof(logo) / sizeof(logo[0]));
    if (n > lines)
        lines = n;
    for (int i = 0; i < lines; i++) {
        printf("%-36s", i < (int)(sizeof(logo) / sizeof(logo[0])) ? logo[i] : "");
        if (i < n)
            printf("%s", info[i]);
        putchar('\n');
    }
    return 0;
}
