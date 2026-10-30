/*
 * ProotOS — ramfs VFS, FHS layout and file-descriptor glue (M6).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "vfs.h"
#include "mm.h"
#include "proc.h"
#include "initramfs.h"
#include "block.h"
#include "ahci.h"
#include "ata.h"
#include "virtio_blk.h"
#include "keyboard.h"

static struct vfs_inode *g_root;

static struct vfs_inode *node_new(const char *name, int type, int mode, struct vfs_super *sb,
                                  struct vfs_inode *parent) {
    struct vfs_inode *n = kzalloc(sizeof(*n));
    if (!n)
        return 0;
    size_t len = strlen(name);
    if (len >= VFS_NAME_MAX)
        len = VFS_NAME_MAX - 1;
    memcpy(n->name, name, len);
    n->name[len] = 0;
    n->type = type;
    n->mode = (uint32_t)mode;
    n->sb = sb;
    n->parent = parent;
    return n;
}

static struct vfs_inode *find_child(struct vfs_inode *dir, const char *name) {
    for (struct vfs_inode *c = dir->children; c; c = c->next) {
        if (strcmp(c->name, name) == 0)
            return c;
    }
    return 0;
}

static void add_child(struct vfs_inode *dir, struct vfs_inode *child) {
    child->parent = dir;
    child->next = dir->children;
    dir->children = child;
}

struct vfs_inode *vfs_lookup(const char *path) {
    if (!g_root || !path)
        return 0;
    struct vfs_inode *cur = g_root;
    const char *p = path;
    while (*p == '/')
        p++;

    while (*p) {
        char comp[VFS_NAME_MAX];
        size_t n = 0;
        while (*p && *p != '/' && n + 1 < sizeof(comp))
            comp[n++] = *p++;
        comp[n] = 0;
        while (*p == '/')
            p++;
        if (!comp[0] || strcmp(comp, ".") == 0)
            continue;
        if (strcmp(comp, "..") == 0) {
            if (cur->parent)
                cur = cur->parent;
            continue;
        }
        if (cur->mounted)
            cur = cur->mounted->root;
        if (cur->type != VFS_DIR)
            return 0;
        struct vfs_inode *next = 0;
        if (cur->sb && cur->sb->lookup)
            next = cur->sb->lookup(cur->sb, cur, comp);
        else
            next = find_child(cur, comp);
        if (!next)
            return 0;
        cur = next;
    }
    if (cur->mounted)
        cur = cur->mounted->root;
    return cur;
}

static struct vfs_inode *resolve_parent(const char *path, char *name, size_t name_size) {
    char buf[256];
    size_t len = strlen(path);
    if (len >= sizeof(buf))
        return 0;
    memcpy(buf, path, len + 1);
    while (len > 1 && buf[len - 1] == '/')
        buf[--len] = 0;
    char *slash = 0;
    for (char *p = buf; *p; p++) {
        if (*p == '/')
            slash = p;
    }
    if (!slash) {
        if (len >= name_size)
            return 0;
        memcpy(name, buf, len + 1);
        return g_root;
    }
    if (strlen(slash + 1) >= name_size)
        return 0;
    strcpy(name, slash + 1);
    *slash = 0;
    if (!buf[0])
        strcpy(buf, "/");
    struct vfs_inode *parent = vfs_lookup(buf);
    if (!parent || parent->type != VFS_DIR)
        return 0;
    return parent;
}

struct vfs_inode *vfs_create(const char *path, int type, int mode) {
    struct vfs_inode *existing = vfs_lookup(path);
    if (existing)
        return existing;

    char name[VFS_NAME_MAX];
    struct vfs_inode *parent = resolve_parent(path, name, sizeof(name));
    if (!parent || !name[0])
        return 0;

    if (parent->sb && parent->sb->create) {
        return parent->sb->create(parent->sb, parent, name, type, mode);
    }
    struct vfs_inode *n = node_new(name, type, mode, parent->sb, parent);
    if (!n)
        return 0;
    if (type == VFS_DIR)
        n->size = 0;
    add_child(parent, n);
    return n;
}

int vfs_unlink(const char *path) {
    char name[VFS_NAME_MAX];
    struct vfs_inode *parent = resolve_parent(path, name, sizeof(name));
    if (!parent || !name[0])
        return -1;
    if (parent->sb && parent->sb->unlink)
        return parent->sb->unlink(parent->sb, parent, name);
    struct vfs_inode **pp = &parent->children;
    while (*pp) {
        if (strcmp((*pp)->name, name) == 0) {
            struct vfs_inode *n = *pp;
            if (n->type == VFS_DIR && n->children)
                return -1;
            *pp = n->next;
            kfree(n->data);
            kfree(n);
            return 0;
        }
        pp = &(*pp)->next;
    }
    return -1;
}

int vfs_read(struct vfs_inode *inode, uint64_t offset, void *buf, size_t len) {
    if (!inode)
        return -1;
    if (inode->type == VFS_DIR)
        return -1;
    if (inode->type == VFS_DEV) {
        if (inode->dev == 1)
            return 0; /* console read not implemented */
        if (inode->dev == 3) {
            int c = keyboard_getchar();
            if (c < 0)
                return 0;
            *(char *)buf = (char)c;
            return 1;
        }
        return 0;
    }
    if (inode->sb && inode->sb->read)
        return (int)inode->sb->read(inode->sb, inode, offset, buf, len);
    if (!inode->data || offset >= inode->size)
        return 0;
    size_t n = inode->size - offset;
    if (n > len)
        n = len;
    memcpy(buf, inode->data + offset, n);
    return (int)n;
}

int vfs_write(struct vfs_inode *inode, uint64_t offset, const void *buf, size_t len) {
    if (!inode)
        return -1;
    if (inode->type == VFS_DIR)
        return -1;
    if (inode->type == VFS_DEV) {
        if (inode->dev == 2)
            return (int)len; /* /dev/null */
        if (inode->dev == 1) {
            char tmp[128];
            const char *s = buf;
            size_t done = 0;
            while (done < len) {
                size_t n = len - done;
                if (n > sizeof(tmp) - 1)
                    n = sizeof(tmp) - 1;
                memcpy(tmp, s + done, n);
                tmp[n] = 0;
                console_write(tmp);
                done += n;
            }
            return (int)done;
        }
        return -1;
    }
    if (inode->sb && inode->sb->write)
        return (int)inode->sb->write(inode->sb, inode, offset, buf, len);

    uint64_t end = offset + len;
    if (end > inode->capacity) {
        uint64_t cap = inode->capacity ? inode->capacity : 64;
        while (cap < end)
            cap *= 2;
        uint8_t *new_data = krealloc(inode->data, cap);
        if (!new_data)
            return -1;
        inode->data = new_data;
        inode->capacity = cap;
    }
    memcpy(inode->data + offset, buf, len);
    if (end > inode->size)
        inode->size = end;
    return (int)len;
}

int vfs_stat(const char *path, struct vfs_stat *out) {
    struct vfs_inode *inode = vfs_lookup(path);
    if (!inode || !out)
        return -1;
    out->size = inode->size;
    out->mode = inode->mode;
    out->type = (uint32_t)inode->type;
    return 0;
}

int vfs_statfs(const char *path, struct vfs_statfs *out) {
    struct vfs_inode *inode = vfs_lookup(path);
    if (!inode || !out)
        return -1;
    if (inode->sb && inode->sb->statfs)
        return inode->sb->statfs(inode->sb, out);
    struct pmm_stats s = pmm_get_stats();
    out->block_size = 4096;
    out->total_blocks = s.total_pages;
    out->free_blocks = s.free_pages;
    return 0;
}

int vfs_readdir(const char *path, uint32_t index, char *name, size_t len, int *type) {
    struct vfs_inode *dir = vfs_lookup(path);
    if (!dir || dir->type != VFS_DIR || !name || len == 0)
        return -1;
    if (dir->sb && dir->sb->readdir)
        return dir->sb->readdir(dir->sb, dir, index, name, len, type);
    uint32_t i = 0;
    for (struct vfs_inode *c = dir->children; c; c = c->next) {
        if (i++ != index)
            continue;
        size_t n = strlen(c->name);
        if (n >= len)
            n = len - 1;
        memcpy(name, c->name, n);
        name[n] = 0;
        if (type)
            *type = c->type;
        return 0;
    }
    return -1;
}

int vfs_mount(const char *path, const char *type) {
    return vfs_mount_block(path, type, ramdisk_device());
}

int vfs_mount_block(const char *path, const char *type, struct block_device *dev) {
    struct vfs_inode *mnt = vfs_lookup(path);
    if (!mnt || mnt->type != VFS_DIR)
        return -1;
    if (mnt->mounted)
        return -1;
    if (strcmp(type, "ext2") == 0) {
        if (!dev)
            return -1;
        struct vfs_super *sb = ext2_mount(dev);
        if (!sb)
            return -1;
        mnt->mounted = sb;
        return 0;
    }
    return -1;
}

int vfs_umount(const char *path) {
    struct vfs_inode *mnt = vfs_lookup(path);
    if (!mnt || !mnt->mounted)
        return -1;
    mnt->mounted = 0;
    return 0;
}

int process_open(const char *path, int flags) {
    struct process *p = process_current();
    if (!p || !path)
        return -1;
    struct vfs_inode *inode = vfs_lookup(path);
    if (!inode && (flags & O_CREAT))
        inode = vfs_create(path, VFS_FILE, 0644);
    if (!inode)
        return -2;
    if ((flags & O_TRUNC) && inode->type == VFS_FILE) {
        inode->size = 0;
    }
    struct file *f = kzalloc(sizeof(*f));
    if (!f)
        return -1;
    f->type = FILE_TYPE_VFS;
    f->inode = inode;
    f->flags = flags;
    f->offset = 0;
    f->refs = 1;
    int fd = process_alloc_fd(p, f);
    if (fd < 0) {
        kfree(f);
        return -1;
    }
    return fd;
}

int process_mkdir(const char *path, int mode) {
    return vfs_create(path, VFS_DIR, mode) ? 0 : -1;
}

int process_unlink(const char *path) {
    return vfs_unlink(path);
}

static int unpack_cb(const char *name, const void *data, size_t size, void *ctx) {
    (void)ctx;
    if (!name[0] || strcmp(name, ".") == 0)
        return 0;
    int type = size ? VFS_FILE : VFS_DIR;
    struct vfs_inode *n = vfs_create(name, type, type == VFS_DIR ? 0755 : 0644);
    if (!n)
        return 0;
    if (type == VFS_FILE && size) {
        if (vfs_write(n, 0, data, size) != (int)size)
            return -1;
    }
    return 0;
}

static void make_dirs(void) {
    static const char *dirs[] = {
        "/bin",  "/sbin", "/lib", "/usr",  "/var",  "/tmp", "/dev",
        "/proc", "/sys",  "/mnt", "/home", "/root", "/etc",
    };
    for (size_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        vfs_create(dirs[i], VFS_DIR, 0755);
    }
}

static void make_proc(void) {
    struct pmm_stats s = pmm_get_stats();
    char text[256];
    struct vfs_inode *n = vfs_create("/proc/meminfo", VFS_FILE, 0444);
    if (n) {
        int len = ksnprintf(text, sizeof(text), "MemTotal: %llu kB\nMemFree: %llu kB\n",
                            (unsigned long long)(s.total_pages * 4),
                            (unsigned long long)(s.free_pages * 4));
        vfs_write(n, 0, text, (size_t)len);
    }
    n = vfs_create("/proc/uptime", VFS_FILE, 0444);
    if (n)
        vfs_write(n, 0, "0.00 0.00\n", 10);
}

static void make_dev(void) {
    struct vfs_inode *n = vfs_create("/dev/console", VFS_DEV, 0600);
    if (n)
        n->dev = 1;
    n = vfs_create("/dev/null", VFS_DEV, 0666);
    if (n)
        n->dev = 2;
    n = vfs_create("/dev/kbd", VFS_DEV, 0444);
    if (n)
        n->dev = 3;
}

void vfs_init(void) {
    g_root = node_new("/", VFS_DIR, 0755, 0, 0);
    if (!g_root)
        panic("vfs: cannot allocate root");
    make_dirs();
    initramfs_iterate(unpack_cb, 0);
    make_proc();
    make_dev();

    const void *ext2_data = 0;
    size_t ext2_size = 0;
    struct block_device *disk = virtio_blk_device();
    if (!disk)
        disk = ata_device();
    if (!disk)
        disk = ahci_device();
    if (disk) {
        vfs_mount_block("/mnt", "ext2", disk);
    } else if (initramfs_find("ext2.img", &ext2_data, &ext2_size) == 0) {
        if (ramdisk_init(ext2_data, ext2_size) == 0)
            vfs_mount("/mnt", "ext2");
    }

    kprintf("[vfs] ramfs root: initramfs unpacked, FHS + /proc + /dev ready\n");
}
