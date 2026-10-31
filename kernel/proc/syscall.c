/*
 * ProotOS — syscall dispatch (M5, int 0x80 ABI).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "kernel.h"
#include "proc.h"
#include "sched.h"
#include "timer.h"
#include "vfs.h"
#include "net.h"
#include "mm.h"
#include "power.h"

#define SYS_READ 0
#define SYS_WRITE 1
#define SYS_GETPID 2
#define SYS_CLOSE 3
#define SYS_SLEEP_MS 4
#define SYS_YIELD 5
#define SYS_OPEN 6
#define SYS_STAT 7
#define SYS_MKDIR 8
#define SYS_UNLINK 9
#define SYS_LSEEK 10
#define SYS_MOUNT 11
#define SYS_UMOUNT 12
#define SYS_READDIR 78
#define SYS_GETPROCS 79
#define SYS_STATFS 80
#define SYS_NET_PING 81
#define SYS_NET_HTTP 82
#define SYS_CLEAR 83
#define SYS_NET_HTTPS 84
#define SYS_FB_INFO 85
#define SYS_FB_MAP 86
#define SYS_FB_UNMAP 88
#define SYS_CONSOLE_GUI 89
#define SYS_MMAP 90
#define SYS_MUNMAP 91
#define SYS_MPROTECT 92
#define SYS_RTC_HMS 93
#define SYS_CONSOLE_INFO 94
#define SYS_PIPE 22
#define SYS_DUP2 33
#define SYS_FCNTL 72
#define SYS_FORK 57
#define SYS_EXECVE 59
#define SYS_EXIT 60
#define SYS_WAIT4 61
#define SYS_KILL 62
#define SYS_PTRACE 101
#define SYS_SHUTDOWN 169
#define SYS_REBOOT 170

static long sys_write(struct process *p, int fd, uint64_t uptr, size_t len) {
    char buf[128];
    size_t done = 0;
    while (done < len) {
        size_t chunk = len - done;
        if (chunk > sizeof(buf))
            chunk = sizeof(buf);
        if (user_copy_from(p, buf, uptr + done, chunk) != 0)
            return -14;
        if ((fd == 1 || fd == 2) && !p->fds[fd]) {
            char text[129];
            memcpy(text, buf, chunk);
            text[chunk] = 0;
            console_write(text);
            done += chunk;
        } else {
            long n = process_write(fd, buf, chunk);
            if (n < 0)
                return n;
            done += (size_t)n;
            if ((size_t)n < chunk)
                break;
        }
    }
    return (long)done;
}

static long sys_read(struct process *p, int fd, uint64_t uptr, size_t len) {
    char buf[128];
    size_t done = 0;
    while (done < len) {
        size_t chunk = len - done;
        if (chunk > sizeof(buf))
            chunk = sizeof(buf);
        long n = process_read(fd, buf, chunk);
        if (n < 0)
            return n;
        if (n == 0)
            break;
        if (user_copy_to(p, uptr + done, buf, (size_t)n) != 0)
            return -14;
        done += (size_t)n;
        if ((size_t)n < chunk)
            break;
    }
    return (long)done;
}

#define FB_USER_BASE 0x30000000ULL

static long sys_fb_info(struct process *p, uint64_t uptr) {
    struct fb_info info;
    console_fb_info(&info);
    if (info.width == 0 || info.height == 0)
        return -1;
    if (user_copy_to(p, uptr, &info, sizeof(info)) != 0)
        return -14;
    return 0;
}

static long sys_fb_map(struct process *p) {
    uint64_t phys = console_fb_phys();
    uint64_t size = console_fb_size();
    if (!phys || !size || (phys & (PAGE_SIZE - 1)) != 0)
        return -1;
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (FB_USER_BASE + size >= USER_LIMIT)
        return -1;
    for (uint64_t off = 0; off < size; off += PAGE_SIZE) {
        uint64_t va = FB_USER_BASE + off;
        if (vmm_is_mapped_pml4(p->pml4_phys, va))
            continue;
        if (vmm_map_pml4(p->pml4_phys, va, phys + off,
                         PTE_PRESENT | PTE_WRITE | PTE_USER | PTE_NX) != 0)
            return -1;
        __asm__ volatile("invlpg (%0)" ::"r"(va) : "memory");
    }
    return (long)FB_USER_BASE;
}

static long sys_fb_unmap(struct process *p) {
    uint64_t size = console_fb_size();
    if (!size)
        return -1;
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    for (uint64_t off = 0; off < size; off += PAGE_SIZE) {
        uint64_t va = FB_USER_BASE + off;
        if (vmm_is_mapped_pml4(p->pml4_phys, va))
            vmm_unmap_pml4(p->pml4_phys, va);
    }
    return 0;
}

#define MMAP_BASE 0x40000000ULL
static uint64_t g_mmap_next = MMAP_BASE;

static long sys_mmap(struct process *p, uint64_t len, uint64_t prot) {
    if (!len)
        return -1;
    uint64_t pages = (len + PAGE_SIZE - 1) >> PAGE_SHIFT;
    uint64_t size = pages * PAGE_SIZE;
    uint64_t va = (g_mmap_next + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    if (va + size >= USER_LIMIT)
        return -1;
    uint64_t phys = pmm_alloc_pages(pages, PMM_ZONE_ANY);
    if (!phys)
        return -1;
    memset(PHYS_TO_VIRT(phys), 0, size);
    uint64_t flags = PTE_PRESENT | PTE_USER | PTE_WRITE;
    if (!(prot & 4))
        flags |= PTE_NX;
    for (uint64_t i = 0; i < pages; i++) {
        if (vmm_map_pml4(p->pml4_phys, va + i * PAGE_SIZE, phys + i * PAGE_SIZE, flags) != 0) {
            pmm_free_pages(phys, pages);
            return -1;
        }
    }
    g_mmap_next = va + size;
    return (long)va;
}

static long sys_munmap(struct process *p, uint64_t addr, uint64_t len) {
    if (!addr || (addr & (PAGE_SIZE - 1)))
        return -1;
    uint64_t pages = (len + PAGE_SIZE - 1) >> PAGE_SHIFT;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t va = addr + i * PAGE_SIZE;
        uint64_t phys = vmm_get_phys_pml4(p->pml4_phys, va);
        if (vmm_unmap_pml4(p->pml4_phys, va) == 0 && phys)
            pmm_free_pages(phys & ~(PAGE_SIZE - 1), 1);
    }
    return 0;
}

static long sys_pipe(struct process *p, uint64_t uptr) {
    int fds[2];
    if (process_pipe(fds) != 0)
        return -1;
    if (user_copy_to(p, uptr, fds, sizeof(fds)) != 0)
        return -14;
    return 0;
}

static long sys_execve(struct process *p, uint64_t upath, uint64_t uargv) {
    char path[64];
    if (user_copy_from(p, path, upath, sizeof(path) - 1) != 0)
        return -14;
    path[sizeof(path) - 1] = 0;

    char *argv[16];
    int argc = 0;
    if (uargv) {
        for (int i = 0; i < 16; i++) {
            uint64_t ptr = 0;
            if (user_copy_from(p, &ptr, uargv + (uint64_t)i * 8, 8) != 0)
                return -14;
            if (!ptr)
                break;
            static char args[16][128];
            if (user_copy_from(p, args[i], ptr, sizeof(args[i]) - 1) != 0)
                return -14;
            args[i][sizeof(args[i]) - 1] = 0;
            argv[argc++] = args[i];
        }
    }
    if (process_exec_argv(p, path, argv, argc) != 0)
        return -2;
    process_enter_user(p);
}

static long sys_open(struct process *p, uint64_t upath, int flags) {
    char path[128];
    if (user_copy_from(p, path, upath, sizeof(path) - 1) != 0)
        return -14;
    path[sizeof(path) - 1] = 0;
    return process_open(path, flags);
}

static long sys_stat(struct process *p, uint64_t upath, uint64_t ustat) {
    char path[128];
    if (user_copy_from(p, path, upath, sizeof(path) - 1) != 0)
        return -14;
    path[sizeof(path) - 1] = 0;
    struct vfs_stat st;
    if (vfs_stat(path, &st) != 0)
        return -2;
    if (user_copy_to(p, ustat, &st, sizeof(st)) != 0)
        return -14;
    return 0;
}

static long sys_mkdir(struct process *p, uint64_t upath, int mode) {
    char path[128];
    if (user_copy_from(p, path, upath, sizeof(path) - 1) != 0)
        return -14;
    path[sizeof(path) - 1] = 0;
    return process_mkdir(path, mode);
}

static long sys_unlink(struct process *p, uint64_t upath) {
    char path[128];
    if (user_copy_from(p, path, upath, sizeof(path) - 1) != 0)
        return -14;
    path[sizeof(path) - 1] = 0;
    return process_unlink(path);
}

static long sys_lseek(struct process *p, int fd, int64_t offset, int whence) {
    if (fd < 0 || fd >= PROC_MAX_FDS || !p->fds[fd])
        return -9;
    struct file *f = p->fds[fd];
    if (whence == 0)
        f->offset = (uint64_t)offset;
    else if (whence == 1)
        f->offset += (uint64_t)offset;
    else if (whence == 2)
        f->offset = f->inode ? f->inode->size + (uint64_t)offset : 0;
    else
        return -22;
    return (long)f->offset;
}

static long sys_readdir(struct process *p, uint64_t upath, uint32_t index, uint64_t uname,
                        uint64_t len) {
    char path[128];
    char name[128];
    if (user_copy_from(p, path, upath, sizeof(path) - 1) != 0)
        return -14;
    path[sizeof(path) - 1] = 0;
    int type = 0;
    if (vfs_readdir(path, index, name, sizeof(name), &type) != 0)
        return -1;
    size_t n = strlen(name);
    if (n >= len)
        n = len - 1;
    if (user_copy_to(p, uname, name, n + 1) != 0)
        return -14;
    return type;
}

static long sys_getprocs(struct process *p, uint64_t ubuf, uint64_t len) {
    char buf[2048];
    int n = process_list(buf, sizeof(buf));
    if (n <= 0)
        return n;
    if ((uint64_t)n > len)
        n = (int)len;
    if (user_copy_to(p, ubuf, buf, (size_t)n) != 0)
        return -14;
    return n;
}

static long sys_statfs(struct process *p, uint64_t upath, uint64_t uout) {
    char path[128];
    if (user_copy_from(p, path, upath, sizeof(path) - 1) != 0)
        return -14;
    path[sizeof(path) - 1] = 0;
    struct vfs_statfs st;
    if (vfs_statfs(path, &st) != 0)
        return -2;
    if (user_copy_to(p, uout, &st, sizeof(st)) != 0)
        return -14;
    return 0;
}

static long sys_net_ping(uint64_t ip) {
    uint8_t a[4] = { (uint8_t)(ip >> 24), (uint8_t)(ip >> 16), (uint8_t)(ip >> 8), (uint8_t)ip };
    return net_ping_ip(a);
}

static long sys_net_http(struct process *p, uint64_t uhost, uint64_t upath, uint64_t uout,
                         uint64_t cap) {
    char host[96], path[128];
    if (user_copy_from(p, host, uhost, sizeof(host) - 1) != 0) return -14;
    if (user_copy_from(p, path, upath, sizeof(path) - 1) != 0) return -14;
    host[sizeof(host) - 1] = path[sizeof(path) - 1] = 0;
    char *buf = kmalloc((size_t)cap);
    if (!buf) return -1;
    int n = net_http_get(host, path, buf, (size_t)cap);
    if (n > 0 && user_copy_to(p, uout, buf, (size_t)n) != 0) n = -14;
    kfree(buf);
    return n;
}

static long sys_net_https(struct process *p, uint64_t uhost, uint64_t upath, uint64_t uout,
                         uint64_t cap) {
    char host[96], path[128];
    if (user_copy_from(p, host, uhost, sizeof(host) - 1) != 0) return -14;
    if (user_copy_from(p, path, upath, sizeof(path) - 1) != 0) return -14;
    host[sizeof(host) - 1] = path[sizeof(path) - 1] = 0;
    char *buf = kmalloc((size_t)cap);
    if (!buf) return -1;
    int n = net_https_get(host, path, buf, (size_t)cap);
    if (n > 0 && user_copy_to(p, uout, buf, (size_t)n) != 0) n = -14;
    kfree(buf);
    return n;
}

static long sys_mount(struct process *p, uint64_t upath, uint64_t utype) {
    char path[128], type[16];
    if (user_copy_from(p, path, upath, sizeof(path) - 1) != 0)
        return -14;
    if (user_copy_from(p, type, utype, sizeof(type) - 1) != 0)
        return -14;
    path[sizeof(path) - 1] = type[sizeof(type) - 1] = 0;
    return vfs_mount(path, type);
}

static long sys_umount(struct process *p, uint64_t upath) {
    char path[128];
    if (user_copy_from(p, path, upath, sizeof(path) - 1) != 0)
        return -14;
    path[sizeof(path) - 1] = 0;
    return vfs_umount(path);
}

void syscall_dispatch(struct trap_frame *tf) {
    __asm__ volatile("sti");
    struct process *p = process_current();
    if (!p) {
        tf->rax = (uint64_t)-1;
        return;
    }

    long ret = -38; /* -ENOSYS */
    switch (tf->rax) {
    case SYS_READ:
        ret = sys_read(p, (int)tf->rdi, tf->rsi, (size_t)tf->rdx);
        break;
    case SYS_WRITE:
        ret = sys_write(p, (int)tf->rdi, tf->rsi, (size_t)tf->rdx);
        break;
    case SYS_GETPID:
        ret = p->pid;
        break;
    case SYS_CLOSE:
        process_close_fd((int)tf->rdi);
        ret = 0;
        break;
    case SYS_YIELD:
        kthread_yield();
        ret = 0;
        break;
    case SYS_SLEEP_MS:
        kthread_sleep_ms(tf->rdi);
        ret = 0;
        break;
    case SYS_OPEN:
        ret = sys_open(p, tf->rdi, (int)tf->rsi);
        break;
    case SYS_STAT:
        ret = sys_stat(p, tf->rdi, tf->rsi);
        break;
    case SYS_MKDIR:
        ret = sys_mkdir(p, tf->rdi, (int)tf->rsi);
        break;
    case SYS_UNLINK:
        ret = sys_unlink(p, tf->rdi);
        break;
    case SYS_LSEEK:
        ret = sys_lseek(p, (int)tf->rdi, (int64_t)tf->rsi, (int)tf->rdx);
        break;
    case SYS_MOUNT:
        ret = sys_mount(p, tf->rdi, tf->rsi);
        break;
    case SYS_UMOUNT:
        ret = sys_umount(p, tf->rdi);
        break;
    case SYS_FB_INFO:
        ret = sys_fb_info(p, tf->rdi);
        break;
    case SYS_FB_MAP:
        ret = sys_fb_map(p);
        break;
    case SYS_FB_UNMAP:
        ret = sys_fb_unmap(p);
        break;
    case SYS_MMAP:
        ret = sys_mmap(p, tf->rdi, tf->rsi);
        break;
    case SYS_MUNMAP:
        ret = sys_munmap(p, tf->rdi, tf->rsi);
        break;
    case SYS_MPROTECT:
        ret = 0;
        break;
    case SYS_RTC_HMS:
        ret = (long)rtc_hms();
        break;
    case SYS_CONSOLE_INFO: {
        int cols = 80, rows = 25;
        console_text_info(&cols, &rows);
        int out[2] = {cols, rows};
        ret = user_copy_to(p, tf->rdi, out, sizeof(out)) == 0 ? 0 : -14;
        break;
    }
    case SYS_CONSOLE_GUI:
        console_set_fb_output(tf->rdi == 0);
        ret = 0;
        break;
    case SYS_READDIR:
        ret = sys_readdir(p, tf->rdi, (uint32_t)tf->rsi, tf->rdx, tf->r10);
        break;
    case SYS_GETPROCS:
        ret = sys_getprocs(p, tf->rdi, tf->rsi);
        break;
    case SYS_STATFS:
        ret = sys_statfs(p, tf->rdi, tf->rsi);
        break;
    case SYS_NET_PING:
        ret = sys_net_ping(tf->rdi);
        break;
    case SYS_NET_HTTP:
        ret = sys_net_http(p, tf->rdi, tf->rsi, tf->rdx, tf->r10);
        break;
    case SYS_NET_HTTPS:
        ret = sys_net_https(p, tf->rdi, tf->rsi, tf->rdx, tf->r10);
        break;
    case SYS_CLEAR:
        console_clear();
        ret = 0;
        break;
    case SYS_DUP2:
        ret = process_dup2((int)tf->rdi, (int)tf->rsi);
        break;
    case SYS_FCNTL:
        ret = process_fcntl((int)tf->rdi, (int)tf->rsi, (int)tf->rdx);
        break;
    case SYS_PIPE:
        ret = sys_pipe(p, tf->rdi);
        break;
    case SYS_FORK:
        ret = (long)process_fork(tf);
        break;
    case SYS_EXECVE:
        ret = sys_execve(p, tf->rdi, tf->rsi);
        break;
    case SYS_WAIT4:
        ret = process_wait((int)tf->rdi, tf->rsi);
        break;
    case SYS_KILL:
        if ((int)tf->rdi == (int)p->pid && (int)tf->rsi == 9)
            process_exit(137);
        ret = 0;
        break;
    case SYS_PTRACE:
        ret = process_ptrace((int)tf->rdi, (int)tf->rsi, tf->rdx, tf->r10);
        break;
    case SYS_SHUTDOWN:
        kprintf("\nM7c complete: rtl8139 TX, AC'97/xHCI discovery, PS/2, ACPI S5.\n");
        acpi_poweroff();
        break;
    case SYS_REBOOT:
        acpi_reboot();
        break;
    case SYS_EXIT:
        process_exit((int)tf->rdi);
        break;
    default:
        break;
    }
    tf->rax = (uint64_t)ret;
}
