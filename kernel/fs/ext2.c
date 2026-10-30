/*
 * ProotOS — minimal ext2 read/write filesystem (M6b).
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "vfs.h"
#include "block.h"
#include "mm.h"

#define EXT2_MAGIC 0xef53
#define EXT2_ROOT_INO 2
#define EXT2_S_IFDIR 0x4000
#define EXT2_S_IFREG 0x8000
#define EXT2_FT_FILE 1
#define EXT2_FT_DIR 2

struct ext2_fs {
    struct block_device *dev;
    struct vfs_super *super;
    uint32_t block_size;
    uint32_t blocks_count;
    uint32_t inodes_count;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t first_data_block;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t groups;
    uint32_t gd_block;
};

static uint16_t rd16(const void *p) {
    return *(const uint16_t *)p;
}
static uint32_t rd32(const void *p) {
    return *(const uint32_t *)p;
}
static void wr16(void *p, uint16_t v) {
    *(uint16_t *)p = v;
}
static void wr32(void *p, uint32_t v) {
    *(uint32_t *)p = v;
}

static int ext2_read_block(struct ext2_fs *fs, uint32_t block, void *buf) {
    if ((uint64_t)block * fs->block_size + fs->block_size > fs->dev->size)
        return -1;
    return fs->dev->read(fs->dev, (uint64_t)block * fs->block_size, buf, fs->block_size);
}

static int ext2_write_block(struct ext2_fs *fs, uint32_t block, const void *buf) {
    if ((uint64_t)block * fs->block_size + fs->block_size > fs->dev->size)
        return -1;
    return fs->dev->write(fs->dev, (uint64_t)block * fs->block_size, buf, fs->block_size);
}

static int ext2_write_super(struct ext2_fs *fs) {
    uint8_t sb[1024];
    if (fs->dev->read(fs->dev, 1024, sb, sizeof(sb)) != 0)
        return -1;
    wr32(sb + 12, fs->free_blocks);
    wr32(sb + 16, fs->free_inodes);
    return fs->dev->write(fs->dev, 1024, sb, sizeof(sb));
}

static int ext2_read_gd(struct ext2_fs *fs, uint32_t group, uint8_t *gd) {
    uint8_t block[4096];
    if (fs->block_size > sizeof(block))
        return -1;
    if (ext2_read_block(fs, fs->gd_block, block) != 0)
        return -1;
    memcpy(gd, block + group * 32, 32);
    return 0;
}

static int ext2_write_gd(struct ext2_fs *fs, uint32_t group, const uint8_t *gd) {
    uint8_t block[4096];
    if (fs->block_size > sizeof(block))
        return -1;
    if (ext2_read_block(fs, fs->gd_block, block) != 0)
        return -1;
    memcpy(block + group * 32, gd, 32);
    return ext2_write_block(fs, fs->gd_block, block);
}

static int ext2_read_inode(struct ext2_fs *fs, uint32_t ino, uint8_t *inode) {
    if (ino == 0 || ino > fs->inodes_count)
        return -1;
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    uint8_t gd[32];
    if (ext2_read_gd(fs, group, gd) != 0)
        return -1;
    uint32_t table = rd32(gd + 8);
    uint64_t off = (uint64_t)table * fs->block_size + (uint64_t)index * fs->inode_size;
    if (off + fs->inode_size > fs->dev->size)
        return -1;
    return fs->dev->read(fs->dev, off, inode, fs->inode_size);
}

static int ext2_write_inode(struct ext2_fs *fs, uint32_t ino, const uint8_t *inode) {
    uint32_t group = (ino - 1) / fs->inodes_per_group;
    uint32_t index = (ino - 1) % fs->inodes_per_group;
    uint8_t gd[32];
    if (ext2_read_gd(fs, group, gd) != 0)
        return -1;
    uint32_t table = rd32(gd + 8);
    uint64_t off = (uint64_t)table * fs->block_size + (uint64_t)index * fs->inode_size;
    return fs->dev->write(fs->dev, off, inode, fs->inode_size);
}

static uint32_t ext2_alloc_block(struct ext2_fs *fs) {
    uint8_t gd[32];
    if (ext2_read_gd(fs, 0, gd) != 0)
        return 0;
    uint32_t bitmap_block = rd32(gd + 0);
    uint8_t bitmap[4096];
    if (ext2_read_block(fs, bitmap_block, bitmap) != 0)
        return 0;
    for (uint32_t i = 0; i < fs->blocks_per_group; i++) {
        uint32_t byte = i / 8, bit = i % 8;
        if (!(bitmap[byte] & (1 << bit))) {
            bitmap[byte] |= (uint8_t)(1 << bit);
            if (ext2_write_block(fs, bitmap_block, bitmap) != 0)
                return 0;
            uint32_t block = fs->first_data_block + i;
            uint8_t zero[4096] = {0};
            ext2_write_block(fs, block, zero);
            uint16_t fb = rd16(gd + 12);
            wr16(gd + 12, (uint16_t)(fb - 1));
            ext2_write_gd(fs, 0, gd);
            fs->free_blocks--;
            ext2_write_super(fs);
            return block;
        }
    }
    return 0;
}

static void ext2_free_block(struct ext2_fs *fs, uint32_t block) {
    if (block < fs->first_data_block)
        return;
    uint8_t gd[32];
    if (ext2_read_gd(fs, 0, gd) != 0)
        return;
    uint32_t bitmap_block = rd32(gd + 0);
    uint8_t bitmap[4096];
    if (ext2_read_block(fs, bitmap_block, bitmap) != 0)
        return;
    uint32_t bit = block - fs->first_data_block;
    bitmap[bit / 8] &= (uint8_t)~(1 << (bit % 8));
    ext2_write_block(fs, bitmap_block, bitmap);
    uint16_t fb = rd16(gd + 12);
    wr16(gd + 12, (uint16_t)(fb + 1));
    ext2_write_gd(fs, 0, gd);
    fs->free_blocks++;
    ext2_write_super(fs);
}

static uint32_t ext2_alloc_inode(struct ext2_fs *fs, uint16_t mode) {
    uint8_t gd[32];
    if (ext2_read_gd(fs, 0, gd) != 0)
        return 0;
    uint32_t bitmap_block = rd32(gd + 4);
    uint8_t bitmap[4096];
    if (ext2_read_block(fs, bitmap_block, bitmap) != 0)
        return 0;
    for (uint32_t i = 0; i < fs->inodes_per_group; i++) {
        uint32_t byte = i / 8, bit = i % 8;
        if (!(bitmap[byte] & (1 << bit))) {
            bitmap[byte] |= (uint8_t)(1 << bit);
            ext2_write_block(fs, bitmap_block, bitmap);
            uint16_t fi = rd16(gd + 14);
            wr16(gd + 14, (uint16_t)(fi - 1));
            ext2_write_gd(fs, 0, gd);
            fs->free_inodes--;
            ext2_write_super(fs);
            uint32_t ino = i + 1;
            uint8_t inode[128] = {0};
            wr16(inode + 0, mode);
            wr16(inode + 26, 1);
            ext2_write_inode(fs, ino, inode);
            return ino;
        }
    }
    return 0;
}

static void ext2_free_inode(struct ext2_fs *fs, uint32_t ino) {
    uint8_t gd[32];
    if (ext2_read_gd(fs, 0, gd) != 0)
        return;
    uint32_t bitmap_block = rd32(gd + 4);
    uint8_t bitmap[4096];
    if (ext2_read_block(fs, bitmap_block, bitmap) != 0)
        return;
    uint32_t bit = ino - 1;
    bitmap[bit / 8] &= (uint8_t)~(1 << (bit % 8));
    ext2_write_block(fs, bitmap_block, bitmap);
    uint16_t fi = rd16(gd + 14);
    wr16(gd + 14, (uint16_t)(fi + 1));
    ext2_write_gd(fs, 0, gd);
    fs->free_inodes++;
    ext2_write_super(fs);
}

static uint32_t ext2_bmap(struct ext2_fs *fs, uint8_t *inode, uint32_t logical, int alloc) {
    uint32_t *blocks = (uint32_t *)(inode + 40);
    if (logical < 12) {
        if (!blocks[logical] && alloc)
            blocks[logical] = ext2_alloc_block(fs);
        return blocks[logical];
    }
    logical -= 12;
    uint32_t per_block = fs->block_size / 4;
    if (logical < per_block) {
        if (!blocks[12] && alloc)
            blocks[12] = ext2_alloc_block(fs);
        if (!blocks[12])
            return 0;
        uint8_t buf[4096];
        if (ext2_read_block(fs, blocks[12], buf) != 0)
            return 0;
        uint32_t *ptrs = (uint32_t *)buf;
        if (!ptrs[logical] && alloc) {
            ptrs[logical] = ext2_alloc_block(fs);
            ext2_write_block(fs, blocks[12], buf);
        }
        return ptrs[logical];
    }
    return 0;
}

static int ext2_dir_add(struct ext2_fs *fs, uint32_t dir_ino, const char *name, uint32_t ino,
                        uint8_t type) {
    uint8_t dir[128];
    if (ext2_read_inode(fs, dir_ino, dir) != 0)
        return -1;
    uint32_t size = rd32(dir + 4);
    uint32_t need = (uint32_t)((8 + strlen(name) + 3) & ~3u);
    uint8_t buf[4096];

    for (uint32_t logical = 0; logical * fs->block_size < size; logical++) {
        uint32_t block = ext2_bmap(fs, dir, logical, 0);
        if (!block)
            continue;
        if (ext2_read_block(fs, block, buf) != 0)
            continue;
        uint32_t off = 0;
        while (off + 8 <= fs->block_size) {
            uint32_t e_ino = rd32(buf + off);
            uint16_t rec = rd16(buf + off + 4);
            if (rec < 8)
                break;
            if (e_ino == 0 && rec >= need) {
                wr32(buf + off, ino);
                buf[off + 6] = (uint8_t)strlen(name);
                buf[off + 7] = type;
                memcpy(buf + off + 8, name, strlen(name));
                ext2_write_block(fs, block, buf);
                return 0;
            }
            uint32_t used = (uint32_t)((8 + buf[off + 6] + 3) & ~3u);
            if (e_ino && rec >= used + need) {
                wr16(buf + off + 4, (uint16_t)used);
                uint32_t noff = off + used;
                wr32(buf + noff, ino);
                wr16(buf + noff + 4, (uint16_t)(rec - used));
                buf[noff + 6] = (uint8_t)strlen(name);
                buf[noff + 7] = type;
                memcpy(buf + noff + 8, name, strlen(name));
                ext2_write_block(fs, block, buf);
                return 0;
            }
            off += rec;
        }
    }

    uint32_t logical = size / fs->block_size;
    uint32_t block = ext2_bmap(fs, dir, logical, 1);
    if (!block)
        return -1;
    memset(buf, 0, fs->block_size);
    wr32(buf, ino);
    wr16(buf + 4, (uint16_t)fs->block_size);
    buf[6] = (uint8_t)strlen(name);
    buf[7] = type;
    memcpy(buf + 8, name, strlen(name));
    if (ext2_write_block(fs, block, buf) != 0)
        return -1;
    wr32(dir + 4, size + fs->block_size);
    ext2_write_inode(fs, dir_ino, dir);
    return 0;
}

static uint32_t ext2_dir_lookup(struct ext2_fs *fs, uint32_t dir_ino, const char *name) {
    uint8_t dir[128];
    if (ext2_read_inode(fs, dir_ino, dir) != 0)
        return 0;
    uint32_t size = rd32(dir + 4);
    uint8_t buf[4096];
    for (uint32_t logical = 0; logical * fs->block_size < size; logical++) {
        uint32_t block = ext2_bmap(fs, dir, logical, 0);
        if (!block)
            continue;
        if (ext2_read_block(fs, block, buf) != 0)
            continue;
        uint32_t off = 0;
        while (off + 8 <= fs->block_size) {
            uint32_t e_ino = rd32(buf + off);
            uint16_t rec = rd16(buf + off + 4);
            uint8_t nlen = buf[off + 6];
            if (rec < 8)
                break;
            if (e_ino && nlen == strlen(name) && memcmp(buf + off + 8, name, nlen) == 0)
                return e_ino;
            off += rec;
        }
    }
    return 0;
}

static struct vfs_inode *ext2_make_inode(struct ext2_fs *fs, uint32_t ino, const char *name,
                                         struct vfs_inode *parent) {
    uint8_t raw[128];
    if (ext2_read_inode(fs, ino, raw) != 0)
        return 0;
    uint16_t mode = rd16(raw + 0);
    struct vfs_inode *n = kzalloc(sizeof(*n));
    if (!n)
        return 0;
    strcpy(n->name, name);
    n->sb = fs->super;
    n->parent = parent;
    n->ino = ino;
    n->mode = mode & 0xfff;
    n->size = rd32(raw + 4);
    n->type = (mode & 0xf000) == EXT2_S_IFDIR ? VFS_DIR : VFS_FILE;
    return n;
}

static struct vfs_inode *ext2_lookup(struct vfs_super *sb, struct vfs_inode *dir,
                                     const char *name) {
    struct ext2_fs *fs = sb->data;
    uint32_t ino = ext2_dir_lookup(fs, dir->ino, name);
    if (!ino)
        return 0;
    return ext2_make_inode(fs, ino, name, dir);
}

static struct vfs_inode *ext2_create(struct vfs_super *sb, struct vfs_inode *dir, const char *name,
                                     int type, int mode) {
    struct ext2_fs *fs = sb->data;
    uint16_t m = (type == VFS_DIR ? EXT2_S_IFDIR : EXT2_S_IFREG) | (mode & 0xfff);
    uint32_t ino = ext2_alloc_inode(fs, m);
    if (!ino)
        return 0;
    if (ext2_dir_add(fs, dir->ino, name, ino, type == VFS_DIR ? EXT2_FT_DIR : EXT2_FT_FILE) != 0)
        return 0;
    if (type == VFS_DIR) {
        uint8_t raw[128];
        ext2_read_inode(fs, ino, raw);
        wr16(raw + 26, 2);
        ext2_write_inode(fs, ino, raw);
        ext2_dir_add(fs, ino, ".", ino, EXT2_FT_DIR);
        ext2_dir_add(fs, ino, "..", dir->ino, EXT2_FT_DIR);
    }
    return ext2_make_inode(fs, ino, name, dir);
}

static int ext2_unlink(struct vfs_super *sb, struct vfs_inode *dir, const char *name) {
    struct ext2_fs *fs = sb->data;
    uint8_t raw[128];
    if (ext2_read_inode(fs, dir->ino, raw) != 0)
        return -1;
    uint32_t size = rd32(raw + 4);
    uint8_t buf[4096];
    for (uint32_t logical = 0; logical * fs->block_size < size; logical++) {
        uint32_t block = ext2_bmap(fs, raw, logical, 0);
        if (!block)
            continue;
        if (ext2_read_block(fs, block, buf) != 0)
            continue;
        uint32_t off = 0, prev = 0;
        while (off + 8 <= fs->block_size) {
            uint32_t e_ino = rd32(buf + off);
            uint16_t rec = rd16(buf + off + 4);
            uint8_t nlen = buf[off + 6];
            if (rec < 8)
                break;
            if (e_ino && nlen == strlen(name) && memcmp(buf + off + 8, name, nlen) == 0) {
                if (prev)
                    wr16(buf + prev + 4, (uint16_t)(rd16(buf + prev + 4) + rec));
                else
                    wr32(buf + off, 0);
                ext2_write_block(fs, block, buf);
                uint8_t inode[128];
                if (ext2_read_inode(fs, e_ino, inode) == 0) {
                    if (rd16(inode + 26) > 1) {
                        wr16(inode + 26, (uint16_t)(rd16(inode + 26) - 1));
                        ext2_write_inode(fs, e_ino, inode);
                    } else {
                        for (int i = 0; i < 15; i++) {
                            uint32_t b = rd32(inode + 40 + i * 4);
                            if (b)
                                ext2_free_block(fs, b);
                        }
                        ext2_free_inode(fs, e_ino);
                    }
                }
                return 0;
            }
            prev = off;
            off += rec;
        }
    }
    return -1;
}

static long ext2_read_file(struct vfs_super *sb, struct vfs_inode *inode, uint64_t offset,
                           void *buf, size_t len) {
    struct ext2_fs *fs = sb->data;
    uint8_t raw[128];
    if (ext2_read_inode(fs, inode->ino, raw) != 0)
        return -1;
    uint32_t size = rd32(raw + 4);
    if (offset >= size)
        return 0;
    if (offset + len > size)
        len = size - offset;
    size_t done = 0;
    uint8_t block[4096];
    while (done < len) {
        uint32_t logical = (uint32_t)((offset + done) / fs->block_size);
        uint32_t boff = (uint32_t)((offset + done) % fs->block_size);
        uint32_t pblock = ext2_bmap(fs, raw, logical, 0);
        size_t n = fs->block_size - boff;
        if (n > len - done)
            n = len - done;
        if (!pblock) {
            memset(block, 0, fs->block_size);
        } else {
            if (ext2_read_block(fs, pblock, block) != 0)
                return -1;
        }
        memcpy((uint8_t *)buf + done, block + boff, n);
        done += n;
    }
    return (long)done;
}

static long ext2_write_file(struct vfs_super *sb, struct vfs_inode *inode, uint64_t offset,
                            const void *buf, size_t len) {
    struct ext2_fs *fs = sb->data;
    uint8_t raw[128];
    if (ext2_read_inode(fs, inode->ino, raw) != 0)
        return -1;
    uint32_t size = rd32(raw + 4);
    size_t done = 0;
    uint8_t block[4096];
    while (done < len) {
        uint32_t logical = (uint32_t)((offset + done) / fs->block_size);
        uint32_t boff = (uint32_t)((offset + done) % fs->block_size);
        uint32_t pblock = ext2_bmap(fs, raw, logical, 1);
        if (!pblock)
            return -1;
        size_t n = fs->block_size - boff;
        if (n > len - done)
            n = len - done;
        if (n != fs->block_size) {
            if (ext2_read_block(fs, pblock, block) != 0)
                return -1;
        }
        memcpy(block + boff, (const uint8_t *)buf + done, n);
        if (ext2_write_block(fs, pblock, block) != 0)
            return -1;
        done += n;
    }
    uint64_t end = offset + len;
    if (end > size) {
        wr32(raw + 4, (uint32_t)end);
        wr32(raw + 28, rd32(raw + 28) + (uint32_t)((len + 511) / 512));
        ext2_write_inode(fs, inode->ino, raw);
        inode->size = end;
    }
    return (long)done;
}

static int ext2_readdir(struct vfs_super *sb, struct vfs_inode *dir, uint32_t index, char *name,
                        size_t len, int *type) {
    struct ext2_fs *fs = sb->data;
    uint8_t raw[128];
    if (ext2_read_inode(fs, dir->ino, raw) != 0)
        return -1;
    uint32_t size = rd32(raw + 4);
    uint32_t seen = 0;
    uint8_t buf[4096];
    for (uint32_t logical = 0; logical * fs->block_size < size; logical++) {
        uint32_t block = ext2_bmap(fs, raw, logical, 0);
        if (!block)
            continue;
        if (ext2_read_block(fs, block, buf) != 0)
            continue;
        uint32_t off = 0;
        while (off + 8 <= fs->block_size) {
            uint32_t e_ino = rd32(buf + off);
            uint16_t rec = rd16(buf + off + 4);
            uint8_t nlen = buf[off + 6];
            if (rec < 8)
                break;
            if (e_ino && nlen) {
                char entry[256];
                uint32_t n = nlen < 255 ? nlen : 255;
                memcpy(entry, buf + off + 8, n);
                entry[n] = 0;
                if (strcmp(entry, ".") && strcmp(entry, "..")) {
                    if (seen++ == index) {
                        size_t out = n;
                        if (out >= len)
                            out = len - 1;
                        memcpy(name, entry, out);
                        name[out] = 0;
                        if (type)
                            *type = buf[off + 7] == EXT2_FT_DIR ? VFS_DIR : VFS_FILE;
                        return 0;
                    }
                }
            }
            off += rec;
        }
    }
    return -1;
}

static int ext2_statfs(struct vfs_super *sb, struct vfs_statfs *out) {
    struct ext2_fs *fs = sb->data;
    out->block_size = fs->block_size;
    out->total_blocks = fs->blocks_count;
    out->free_blocks = fs->free_blocks;
    return 0;
}

struct vfs_super *ext2_mount(struct block_device *dev) {
    if (!dev)
        return 0;
    uint8_t sb[1024];
    if (dev->read(dev, 1024, sb, sizeof(sb)) != 0)
        return 0;
    if (rd16(sb + 56) != EXT2_MAGIC) {
        kprintf("[ext2] bad magic\n");
        return 0;
    }
    struct ext2_fs *fs = kzalloc(sizeof(*fs));
    if (!fs)
        return 0;
    fs->dev = dev;
    fs->block_size = 1024u << rd32(sb + 24);
    if (fs->block_size > 4096)
        return 0;
    fs->blocks_count = rd32(sb + 4);
    fs->inodes_count = rd32(sb + 0);
    fs->free_blocks = rd32(sb + 12);
    fs->free_inodes = rd32(sb + 16);
    fs->first_data_block = rd32(sb + 20);
    fs->blocks_per_group = rd32(sb + 32);
    fs->inodes_per_group = rd32(sb + 40);
    fs->inode_size = rd32(sb + 88);
    if (fs->inode_size < 128 || fs->inode_size > 512)
        fs->inode_size = 128;
    fs->groups = (fs->blocks_count + fs->blocks_per_group - 1) / fs->blocks_per_group;
    fs->gd_block = fs->first_data_block + 1;

    struct vfs_super *super = kzalloc(sizeof(*super));
    if (!super)
        return 0;
    super->type = 2;
    super->data = fs;
    super->lookup = ext2_lookup;
    super->create = ext2_create;
    super->unlink = ext2_unlink;
    super->read = ext2_read_file;
    super->write = ext2_write_file;
    super->readdir = ext2_readdir;
    super->statfs = ext2_statfs;
    fs->super = super;
    super->root = ext2_make_inode(fs, EXT2_ROOT_INO, "/", 0);
    kprintf("[ext2] mounted: %u KiB blocks, %u inodes, %u/%u blocks free\n", fs->block_size,
            fs->inodes_count, fs->free_blocks, fs->blocks_count);
    return super;
}
