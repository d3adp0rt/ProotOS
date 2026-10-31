#include "libc.h"

static void cat_file(const char *path) {
    int fd = open(path, 0, 0);
    if (fd < 0) {
        printf("cat: cannot open %s\n", path);
        return;
    }
    char buf[256];
    long n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        write(1, buf, (size_t)n);
    close(fd);
}

static char g_history[8][128];
static int g_hist_count;

static int run(char *line) {
    char *argv[16];
    int argc = 0;
    char *p = line;
    while (*p && argc < 15) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p)
            *p++ = 0;
    }
    argv[argc] = 0;
    if (!argc)
        return 0;
    if (strcmp(argv[0], "exit") == 0)
        return 1;
    if (strcmp(argv[0], "shutdown") == 0) {
        shutdown_os();
        return 1;
    }
    if (strcmp(argv[0], "help") == 0) {
        puts("builtins: help echo cat clear history exit shutdown; external: /bin/*");
        return 0;
    }
    if (strcmp(argv[0], "clear") == 0) { clear_screen(); return 0; }
    if (strcmp(argv[0], "history") == 0) {
        for (int i = 0; i < 8; i++) if (g_history[i][0]) printf("%d %s\n", i + 1, g_history[i]);
        return 0;
    }
    if (strcmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            if (i > 1)
                putchar(' ');
            printf("%s", argv[i]);
        }
        putchar('\n');
        return 0;
    }
    if (strcmp(argv[0], "cat") == 0) {
        if (argc < 2)
            cat_file("/etc/motd");
        else
            for (int i = 1; i < argc; i++)
                cat_file(argv[i]);
        return 0;
    }
    char path[64];
    if (argv[0][0] == '/')
        strcpy(path, argv[0]);
    else {
        strcpy(path, "/bin/");
        strcpy(path + 5, argv[0]);
    }
    int pid = fork();
    if (pid == 0) {
        execve(path, argv, 0);
        printf("sh: exec failed: %s\n", path);
        _exit(127);
    }
    int status = 0;
    wait4(pid, &status, 0, 0);
    return 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    puts("ProotOS shell (M8)");
    cat_file("/etc/motd");

    char *demo[] = {"echo", "M8 libc + shell + /bin/echo works", 0};
    int pid = fork();
    if (pid == 0) {
        execve("/bin/echo", demo, 0);
        _exit(1);
    }
    int st = 0;
    wait4(pid, &st, 0, 0);
    run("ls /");
    run("ps");
    run("df /");
    run("ping 10.0.2.2");
    run("wget example.com /");
    run("wget example.com /");
    run("ppkg update");
    run("ppkg list");
    run("ppkg install hello");
    run("/usr/bin/greet");
    run("/usr/bin/hello");
    run("ppkg remove hello");
    run("ppkg remove greet");
    puts("M8b complete: libc, shell, ls/rm/cp/mv/ps/df.");

    int kbd = open("/dev/kbd", 0, 0);
    if (kbd < 0) {
        puts("sh: no /dev/kbd");
        return 1;
    }

    char line[128];
    int len = 0, prev_len = 0, hist_pos = 0;
    for (;;) {
        printf("proot> ");
        len = 0;
        prev_len = 0;
        hist_pos = g_hist_count;
        for (;;) {
            char c;
            long n = read(kbd, &c, 1);
            if (n <= 0) { sleep_ms(10); continue; }
            if (c == '\n' || c == '\r') {
                putchar('\n');
                line[len] = 0;
                break;
            }
            if (c == 8 || c == 127) {
                if (len > 0) { len--; printf("\b \b"); }
                continue;
            }
            if ((unsigned char)c == 0x80) { /* up */
                if (g_hist_count && hist_pos > 0) hist_pos--;
                if (g_hist_count) {
                    strcpy(line, g_history[hist_pos]);
                    len = (int)strlen(line);
                    printf("\rproot> %s", line);
                    for (int i = len; i < prev_len + 8; i++) putchar(' ');
                    printf("\rproot> %s", line);
                    prev_len = len;
                }
                continue;
            }
            if ((unsigned char)c == 0x81) { /* down */
                if (hist_pos < g_hist_count) hist_pos++;
                if (hist_pos < g_hist_count) strcpy(line, g_history[hist_pos]);
                else line[0] = 0;
                len = (int)strlen(line);
                printf("\rproot> %s", line);
                for (int i = len; i < prev_len + 8; i++) putchar(' ');
                printf("\rproot> %s", line);
                prev_len = len;
                continue;
            }
            if ((unsigned char)c >= 0x80) continue;
            if (len < (int)sizeof(line) - 1) { line[len++] = c; putchar(c); }
        }
        if (len > 0) {
            strcpy(g_history[g_hist_count % 8], line);
            g_hist_count++;
            if (g_hist_count > 8) {
                for (int i = 0; i < 7; i++) strcpy(g_history[i], g_history[i + 1]);
                g_hist_count = 7;
            }
        }
        if (run(line)) break;
    }
    return 0;
}
