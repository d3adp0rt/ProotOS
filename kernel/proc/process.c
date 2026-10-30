/*
 * ProotOS — user process creation and ELF loading (M5).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "proc.h"
#include "mm.h"
#include "gdt.h"
#include "initramfs.h"
#include "timer.h"

typedef struct {
    uint8_t e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

#define PT_LOAD 1
#define EM_X86_64 62
#define ET_EXEC 2
#define PF_X 1
#define PF_W 2

static uint32_t g_next_pid = 1;
static struct process *g_processes;

void process_init(void) {
    g_processes = 0;
    kprintf("[proc] process layer ready\n");
}

struct process *process_current(void) {
    struct thread *t = sched_current();
    return t ? t->proc : 0;
}

static int map_user_pages(struct process *p, uint64_t vaddr, uint64_t phys, uint64_t pages,
                          uint64_t flags) {
    for (uint64_t i = 0; i < pages; i++) {
        if (vmm_map_pml4(p->pml4_phys, vaddr + i * PAGE_SIZE, phys + i * PAGE_SIZE, flags) != 0) {
            return -1;
        }
    }
    return 0;
}

static int elf_load_user(struct process *p, const void *data, size_t size) {
    if (size < sizeof(Elf64_Ehdr))
        return -1;
    const Elf64_Ehdr *eh = data;
    if (memcmp(eh->e_ident,
               "\x7f"
               "ELF",
               4) != 0 ||
        eh->e_ident[4] != 2 || eh->e_ident[5] != 1)
        return -1;
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_X86_64)
        return -1;
    if (!eh->e_phnum || eh->e_phentsize != sizeof(Elf64_Phdr))
        return -1;

    uint64_t max_end = 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)((const uint8_t *)data + eh->e_phoff +
                                                    (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
            continue;
        if (ph->p_vaddr + ph->p_memsz > USER_LIMIT)
            return -1;
        if (ph->p_offset + ph->p_filesz > size)
            return -1;

        uint64_t vstart = ph->p_vaddr & ~(PAGE_SIZE - 1);
        uint64_t voff = ph->p_vaddr - vstart;
        uint64_t pages = (voff + ph->p_memsz + PAGE_SIZE - 1) >> PAGE_SHIFT;
        uint64_t phys = pmm_alloc_pages(pages, PMM_ZONE_ANY);
        if (!phys)
            return -1;
        memset(PHYS_TO_VIRT(phys), 0, pages * PAGE_SIZE);
        memcpy((uint8_t *)PHYS_TO_VIRT(phys) + voff, (const uint8_t *)data + ph->p_offset,
               ph->p_filesz);

        uint64_t flags = PTE_PRESENT | PTE_USER | PTE_WRITE;
        if (ph->p_flags & PF_X)
            flags &= ~PTE_NX;
        else
            flags |= PTE_NX;
        if (map_user_pages(p, vstart, phys, pages, flags) != 0)
            return -1;
        if (ph->p_vaddr + ph->p_memsz > max_end)
            max_end = ph->p_vaddr + ph->p_memsz;
    }

    uint64_t stack_base = USER_STACK_TOP - USER_STACK_PAGES * PAGE_SIZE;
    uint64_t stack_phys = pmm_alloc_pages(USER_STACK_PAGES, PMM_ZONE_ANY);
    if (!stack_phys)
        return -1;
    memset(PHYS_TO_VIRT(stack_phys), 0, USER_STACK_PAGES * PAGE_SIZE);
    if (map_user_pages(p, stack_base, stack_phys, USER_STACK_PAGES,
                       PTE_PRESENT | PTE_USER | PTE_WRITE | PTE_NX) != 0)
        return -1;

    p->entry = eh->e_entry;
    p->user_stack_top = USER_STACK_TOP - 16;
    p->brk = (max_end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    return 0;
}

static void user_entry(void *arg) {
    struct thread *cur = sched_current();
    struct process *p = arg ? arg : cur->proc;
    p->thread = cur;
    cur->proc = p;
    process_enter_user(p);
}

void process_enter_user(struct process *p) {
    struct thread *cur = sched_current();
    cur->proc = p;
    p->thread = cur;
    cur->pml4_phys = p->pml4_phys;
    vmm_switch(p->pml4_phys);
    tss_set_rsp0((uint64_t)(uintptr_t)(cur->stack + cur->stack_size));

    kprintf("[proc] entering user pid=%u entry=%p stack=%p\n", p->pid, (void *)p->entry,
            (void *)p->user_stack_top);

    uint64_t kstack = ((uint64_t)(uintptr_t)(cur->stack + cur->stack_size)) & ~0xfULL;
    __asm__ volatile("movq %0, %%rsp\n"
                     "pushq $0x23\n"
                     "pushq %1\n"
                     "pushq $0x202\n"
                     "pushq $0x1b\n"
                     "pushq %2\n"
                     "iretq\n"
                     :
                     : "r"(kstack), "r"(p->user_stack_top), "r"(p->entry)
                     : "memory");
    __builtin_unreachable();
}

int process_exec(struct process *p, const char *name) {
    const void *data = 0;
    size_t size = 0;
    if (!p || initramfs_find(name, &data, &size) != 0)
        return -1;

    struct process tmp = *p;
    tmp.pml4_phys = vmm_create_pml4();
    if (!tmp.pml4_phys)
        return -1;
    if (elf_load_user(&tmp, data, size) != 0)
        return -1;

    p->pml4_phys = tmp.pml4_phys;
    p->entry = tmp.entry;
    p->user_stack_top = tmp.user_stack_top;
    p->brk = tmp.brk;
    return 0;
}

void process_segfault(void) {
    process_exit(139);
}

struct process *process_find(uint32_t pid) {
    for (struct process *p = g_processes; p; p = p->next_all) {
        if (p->pid == pid)
            return p;
    }
    return 0;
}

long process_ptrace(int request, int pid, uint64_t addr, uint64_t data) {
    struct process *cur = process_current();
    switch (request) {
    case 0: /* PTRACE_TRACEME */
        if (cur)
            cur->traced = 1;
        return 0;
    case 16: { /* PTRACE_ATTACH */
        struct process *t = process_find((uint32_t)pid);
        if (!t)
            return -1;
        t->traced = 1;
        return 0;
    }
    case 2: { /* PTRACE_PEEKDATA */
        struct process *t = process_find((uint32_t)pid);
        if (!t)
            return -1;
        uint64_t value = 0;
        if (user_copy_from(t, &value, addr, sizeof(value)) != 0)
            return -1;
        return (long)value;
    }
    case 5: { /* PTRACE_POKEDATA */
        struct process *t = process_find((uint32_t)pid);
        if (!t)
            return -1;
        if (user_copy_to(t, addr, &data, sizeof(data)) != 0)
            return -1;
        return 0;
    }
    case 7: /* PTRACE_CONT */
        return 0;
    default:
        return -1;
    }
}

struct fork_ctx {
    struct process *child;
    uint64_t rip;
    uint64_t rsp;
    uint64_t rflags;
    uint64_t cs;
    uint64_t ss;
};
static void fork_child_entry(void *arg) {
    struct fork_ctx *ctx = arg;
    struct thread *cur = sched_current();
    struct process *p = ctx->child;
    uint64_t rip = ctx->rip;
    uint64_t rsp = ctx->rsp;
    uint64_t rflags = ctx->rflags;
    uint64_t cs = ctx->cs;
    uint64_t ss = ctx->ss;
    kfree(ctx);

    cur->proc = p;
    p->thread = cur;
    cur->pml4_phys = p->pml4_phys;
    vmm_switch(p->pml4_phys);
    tss_set_rsp0((uint64_t)(uintptr_t)(cur->stack + cur->stack_size));
    kprintf("[proc] fork child pid=%u\n", p->pid);

    uint64_t kstack = ((uint64_t)(uintptr_t)(cur->stack + cur->stack_size)) & ~0xfULL;
    __asm__ volatile("movq %0, %%rsp\n"
                     "xorq %%rax, %%rax\n"
                     "pushq %5\n"
                     "pushq %1\n"
                     "pushq %2\n"
                     "pushq %3\n"
                     "pushq %4\n"
                     "iretq\n"
                     :
                     : "r"(kstack), "r"(rsp), "r"(rflags), "r"(cs), "r"(rip), "r"(ss)
                     : "memory");
    __builtin_unreachable();
}

uint32_t process_fork(struct trap_frame *tf) {
    struct process *parent = process_current();
    if (!parent)
        return (uint32_t)-1;

    struct process *child = kzalloc(sizeof(*child));
    if (!child)
        return (uint32_t)-1;
    child->pid = __atomic_add_fetch(&g_next_pid, 1, __ATOMIC_RELAXED);
    child->ppid = parent->pid;
    child->state = PROC_RUNNING;
    child->parent = parent;
    child->pml4_phys = vmm_create_pml4();
    if (!child->pml4_phys)
        return (uint32_t)-1;
    child->child_wq.head = 0;
    child->child_wq.lock = (spinlock_t)SPINLOCK_INIT;
    child->entry = parent->entry;
    child->user_stack_top = parent->user_stack_top;
    child->brk = parent->brk;

    if (vmm_copy_user_space(parent->pml4_phys, child->pml4_phys) != 0)
        return (uint32_t)-1;

    child->sibling = parent->children;
    parent->children = child;
    child->next_all = g_processes;
    g_processes = child;
    process_fd_copy(parent, child);

    struct fork_ctx *ctx = kzalloc(sizeof(*ctx));
    if (!ctx)
        return (uint32_t)-1;
    ctx->child = child;
    ctx->rip = tf->rip;
    ctx->rsp = tf->rsp;
    ctx->rflags = tf->rflags;
    ctx->cs = tf->cs;
    ctx->ss = tf->ss;

    struct thread *t = kthread_create(fork_child_entry, ctx, "fork-child", SCHED_NORMAL, 0, 0);
    if (!t)
        return (uint32_t)-1;
    t->proc = child;
    t->pml4_phys = child->pml4_phys;
    return child->pid;
}

int process_wait(int pid, uint64_t status_uptr) {
    struct process *parent = process_current();
    if (!parent)
        return -1;

    for (;;) {
        for (struct process *child = parent->children; child; child = child->sibling) {
            if (pid != -1 && (int)child->pid != pid)
                continue;
            if (child->state != PROC_ZOMBIE)
                continue;
            if (status_uptr) {
                int status = (child->exit_code & 0xff) << 8;
                if (user_copy_to(parent, status_uptr, &status, sizeof(status)) != 0)
                    return -1;
            }
            return (int)child->pid;
        }
        wait_queue_wait(&parent->child_wq);
    }
}

struct process *process_spawn_init(void) {
    const void *data = 0;
    size_t size = 0;
    if (initramfs_find("init", &data, &size) != 0) {
        kprintf("[proc] no /init in initramfs\n");
        return 0;
    }

    struct process *p = kzalloc(sizeof(*p));
    if (!p)
        return 0;
    p->pid = __atomic_add_fetch(&g_next_pid, 1, __ATOMIC_RELAXED);
    p->ppid = 0;
    p->state = PROC_RUNNING;
    p->pml4_phys = vmm_create_pml4();
    if (!p->pml4_phys)
        return 0;
    p->next_all = g_processes;
    g_processes = p;
    p->child_wq.head = 0;
    p->child_wq.lock = (spinlock_t)SPINLOCK_INIT;

    if (elf_load_user(p, data, size) != 0) {
        kprintf("[proc] failed to load /init ELF\n");
        return 0;
    }

    struct thread *t = kthread_create(user_entry, p, "init", SCHED_NORMAL, 0, 0);
    if (!t)
        return 0;
    t->proc = p;
    t->pml4_phys = p->pml4_phys;
    p->thread = t;
    kprintf("[proc] spawned /init pid=%u, %llu bytes\n", p->pid, (unsigned long long)size);
    return p;
}

void process_exit(int code) {
    struct process *p = process_current();
    if (!p)
        kthread_exit();
    p->state = PROC_ZOMBIE;
    p->exit_code = code;
    for (int i = 0; i < PROC_MAX_FDS; i++)
        process_close_fd(i);
    if (p->parent)
        wait_queue_wake_all(&p->parent->child_wq);
    kprintf("[proc] pid=%u exited with %d\n", p->pid, code);
    if (!p->parent) {
        kprintf("\nM5 complete: user mode, int 0x80 syscalls, ELF64 loader, fork/execve/wait, "
                "pipes.\n");
    }
    kthread_exit();
}

int user_copy_from(struct process *p, void *dst, uint64_t uptr, size_t len) {
    if (!p || !len)
        return 0;
    if (uptr + len < uptr || uptr + len > USER_LIMIT)
        return -1;
    uint8_t *d = dst;
    while (len) {
        uint64_t page = uptr & ~(PAGE_SIZE - 1);
        if (!vmm_is_mapped_pml4(p->pml4_phys, page))
            return -1;
        uint64_t phys = vmm_get_phys_pml4(p->pml4_phys, uptr);
        size_t chunk = PAGE_SIZE - (uptr & (PAGE_SIZE - 1));
        if (chunk > len)
            chunk = len;
        memcpy(d, PHYS_TO_VIRT(phys), chunk);
        d += chunk;
        uptr += chunk;
        len -= chunk;
    }
    return 0;
}

int user_copy_to(struct process *p, uint64_t uptr, const void *src, size_t len) {
    if (!p || !len)
        return 0;
    if (uptr + len < uptr || uptr + len > USER_LIMIT)
        return -1;
    const uint8_t *s = src;
    while (len) {
        uint64_t page = uptr & ~(PAGE_SIZE - 1);
        if (!vmm_is_mapped_pml4(p->pml4_phys, page))
            return -1;
        uint64_t phys = vmm_get_phys_pml4(p->pml4_phys, uptr);
        size_t chunk = PAGE_SIZE - (uptr & (PAGE_SIZE - 1));
        if (chunk > len)
            chunk = len;
        memcpy(PHYS_TO_VIRT(phys), s, chunk);
        s += chunk;
        uptr += chunk;
        len -= chunk;
    }
    return 0;
}
