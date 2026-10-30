/*
 * ProotOS — pipes and file descriptors (M5b).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "proc.h"
#include "mm.h"

#define PIPE_TYPE 2
#define PIPE_CAP 4096
#define PIPE_READ 0
#define PIPE_WRITE 1

struct pipe {
    uint8_t buf[PIPE_CAP];
    size_t read_pos;
    size_t write_pos;
    size_t count;
    int readers;
    int writers;
    struct wait_queue read_wq;
    struct wait_queue write_wq;
    spinlock_t lock;
};

void process_fd_copy(struct process *parent, struct process *child);

static int fd_alloc(struct process *p, struct file *f) {
    for (int i = 3; i < PROC_MAX_FDS; i++) {
        if (!p->fds[i]) {
            p->fds[i] = f;
            return i;
        }
    }
    return -1;
}

int process_pipe(int out_fds[2]) {
    struct process *p = process_current();
    if (!p)
        return -1;

    struct pipe *pipe = kzalloc(sizeof(*pipe));
    struct file *r = kzalloc(sizeof(*r));
    struct file *w = kzalloc(sizeof(*w));
    if (!pipe || !r || !w)
        return -1;

    pipe->readers = 1;
    pipe->writers = 1;
    pipe->read_wq.head = 0;
    pipe->write_wq.head = 0;
    pipe->read_wq.lock = (spinlock_t)SPINLOCK_INIT;
    pipe->write_wq.lock = (spinlock_t)SPINLOCK_INIT;
    pipe->lock = (spinlock_t)SPINLOCK_INIT;

    r->type = PIPE_TYPE;
    r->pipe = pipe;
    r->write_end = 0;
    r->refs = 1;
    w->type = PIPE_TYPE;
    w->pipe = pipe;
    w->write_end = 1;
    w->refs = 1;

    int rfd = fd_alloc(p, r);
    int wfd = fd_alloc(p, w);
    if (rfd < 0 || wfd < 0)
        return -1;
    out_fds[0] = rfd;
    out_fds[1] = wfd;
    return 0;
}

void process_close_fd(int fd) {
    struct process *p = process_current();
    if (!p || fd < 0 || fd >= PROC_MAX_FDS || !p->fds[fd])
        return;
    struct file *f = p->fds[fd];
    p->fds[fd] = 0;
    f->refs--;
    if (f->refs > 0)
        return;
    if (f->type == PIPE_TYPE && f->pipe) {
        struct pipe *pipe = f->pipe;
        if (f->write_end)
            pipe->writers--;
        else
            pipe->readers--;
        wait_queue_wake_all(&pipe->read_wq);
        wait_queue_wake_all(&pipe->write_wq);
    }
    kfree(f);
}

long process_read(int fd, void *buf, size_t len) {
    struct process *p = process_current();
    if (!p || fd < 3 || fd >= PROC_MAX_FDS || !p->fds[fd])
        return -9;
    struct file *f = p->fds[fd];
    if (f->type != PIPE_TYPE || f->write_end)
        return -9;
    struct pipe *pipe = f->pipe;

    size_t done = 0;
    uint8_t *dst = buf;
    while (done < len) {
        spin_lock(&pipe->lock);
        while (pipe->count == 0 && pipe->writers > 0) {
            spin_unlock(&pipe->lock);
            wait_queue_wait(&pipe->read_wq);
            spin_lock(&pipe->lock);
        }
        if (pipe->count == 0 && pipe->writers == 0) {
            spin_unlock(&pipe->lock);
            break;
        }
        while (done < len && pipe->count) {
            dst[done++] = pipe->buf[pipe->read_pos];
            pipe->read_pos = (pipe->read_pos + 1) % PIPE_CAP;
            pipe->count--;
        }
        spin_unlock(&pipe->lock);
        wait_queue_wake_all(&pipe->write_wq);
    }
    return (long)done;
}

long process_write(int fd, const void *buf, size_t len) {
    struct process *p = process_current();
    if (!p || fd < 3 || fd >= PROC_MAX_FDS || !p->fds[fd])
        return -9;
    struct file *f = p->fds[fd];
    if (f->type != PIPE_TYPE || !f->write_end)
        return -9;
    struct pipe *pipe = f->pipe;

    size_t done = 0;
    const uint8_t *src = buf;
    while (done < len) {
        spin_lock(&pipe->lock);
        if (pipe->readers == 0) {
            spin_unlock(&pipe->lock);
            return -32; /* EPIPE */
        }
        while (pipe->count == PIPE_CAP && pipe->readers > 0) {
            spin_unlock(&pipe->lock);
            wait_queue_wait(&pipe->write_wq);
            spin_lock(&pipe->lock);
        }
        while (done < len && pipe->count < PIPE_CAP) {
            pipe->buf[pipe->write_pos] = src[done++];
            pipe->write_pos = (pipe->write_pos + 1) % PIPE_CAP;
            pipe->count++;
        }
        spin_unlock(&pipe->lock);
        wait_queue_wake_all(&pipe->read_wq);
    }
    return (long)done;
}

void process_fd_copy(struct process *parent, struct process *child) {
    for (int i = 0; i < PROC_MAX_FDS; i++) {
        struct file *f = parent->fds[i];
        child->fds[i] = f;
        if (!f)
            continue;
        f->refs++;
        if (f->type == PIPE_TYPE && f->pipe) {
            if (f->write_end)
                f->pipe->writers++;
            else
                f->pipe->readers++;
        }
    }
}
