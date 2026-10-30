/*
 * ProotOS — VFS and ramfs interfaces (M6).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include "kernel.h"

#define VFS_NAME_MAX 64
#define VFS_FILE 1
#define VFS_DIR 2
#define VFS_DEV 3

struct vfs_inode;
struct vfs_super;

struct vfs_super {
    int type;
    void *data;
    struct vfs_inode *root;
    struct vfs_inode *(*lookup)(struct vfs_super *sb, struct vfs_inode *dir, const char *name);
    struct vfs_inode *(*create)(struct vfs_super *sb, struct vfs_inode *dir, const char *name,
                                int type, int mode);
    int (*unlink)(struct vfs_super *sb, struct vfs_inode *dir, const char *name);
    long (*read)(struct vfs_super *sb, struct vfs_inode *inode, uint64_t offset, void *buf,
                 size_t len);
    long (*write)(struct vfs_super *sb, struct vfs_inode *inode, uint64_t offset, const void *buf,
                  size_t len);
};

struct vfs_inode {
    char name[VFS_NAME_MAX];
    int type;
    uint32_t mode;
    uint64_t size;
    uint8_t *data;
    uint64_t capacity;
    uint32_t ino;
    int dev;
    struct vfs_super *sb;
    struct vfs_inode *parent;
    struct vfs_inode *children;
    struct vfs_inode *next;
    struct vfs_super *mounted;
};

struct vfs_stat {
    uint64_t size;
    uint32_t mode;
    uint32_t type;
};

void vfs_init(void);
struct vfs_inode *vfs_lookup(const char *path);
int vfs_read(struct vfs_inode *inode, uint64_t offset, void *buf, size_t len);
int vfs_write(struct vfs_inode *inode, uint64_t offset, const void *buf, size_t len);
struct vfs_inode *vfs_create(const char *path, int type, int mode);
int vfs_unlink(const char *path);
int vfs_stat(const char *path, struct vfs_stat *out);
int vfs_mount(const char *path, const char *type);
int vfs_umount(const char *path);
