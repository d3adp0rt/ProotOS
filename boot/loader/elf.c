/*
 * ProotOS — minimal ELF64 loader used by the UEFI bootloader.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "loader.h"

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

static uint64_t align_down(uint64_t value, uint64_t align) {
    return value & ~(align - 1);
}

static uint64_t align_up(uint64_t value, uint64_t align) {
    return (value + align - 1) & ~(align - 1);
}

int elf_load(const struct file_blob *blob, struct loaded_elf *out) {
    memset(out, 0, sizeof(*out));

    if (blob->size < sizeof(Elf64_Ehdr)) {
        console_puts("[loader] kernel file too small\n");
        return -1;
    }
    const Elf64_Ehdr *eh = blob->data;
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' ||
        eh->e_ident[3] != 'F') {
        console_puts("[loader] kernel is not ELF\n");
        return -1;
    }
    if (eh->e_ident[4] != 2 || eh->e_ident[5] != 1) {
        console_puts("[loader] kernel is not ELF64 little-endian\n");
        return -1;
    }
    if (eh->e_type != ET_EXEC || eh->e_machine != EM_X86_64) {
        console_printf("[loader] unsupported ELF (type=%u machine=%u)\n", eh->e_type,
                       eh->e_machine);
        return -1;
    }
    if (eh->e_phnum == 0 || eh->e_phentsize != sizeof(Elf64_Phdr)) {
        console_puts("[loader] unsupported program header table\n");
        return -1;
    }

    uint64_t min_vaddr = ~0ULL;
    uint64_t max_vaddr = 0;
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)((const uint8_t *)blob->data + eh->e_phoff +
                                                    (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD)
            continue;
        if (ph->p_memsz == 0)
            continue;
        if (ph->p_offset + ph->p_filesz > blob->size) {
            console_puts("[loader] ELF segment outside file\n");
            return -1;
        }
        if (ph->p_vaddr < min_vaddr)
            min_vaddr = ph->p_vaddr;
        if (ph->p_vaddr + ph->p_memsz > max_vaddr)
            max_vaddr = ph->p_vaddr + ph->p_memsz;
    }
    if (min_vaddr == ~0ULL || max_vaddr <= min_vaddr) {
        console_puts("[loader] no loadable ELF segments\n");
        return -1;
    }

    uint64_t virt_base = align_down(min_vaddr, 4096);
    uint64_t virt_end = align_up(max_vaddr, 4096);
    uint64_t virt_size = virt_end - virt_base;

    uint64_t phys_base = uefi_alloc_pages(virt_size, 0xffffffffULL);
    memset((void *)(uintptr_t)phys_base, 0, virt_size);

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph = (const Elf64_Phdr *)((const uint8_t *)blob->data + eh->e_phoff +
                                                    (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0)
            continue;
        uint8_t *dst = (uint8_t *)(uintptr_t)(phys_base + (ph->p_vaddr - virt_base));
        const uint8_t *src = (const uint8_t *)blob->data + ph->p_offset;
        memcpy(dst, src, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz) {
            memset(dst + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
        }
    }

    out->virt_base = virt_base;
    out->virt_size = virt_size;
    out->phys_base = phys_base;
    out->entry = eh->e_entry;

    console_printf("[loader] ELF: %llu bytes, virt %p..%p, phys %p, entry %p\n",
                   (unsigned long long)virt_size, (void *)virt_base,
                   (void *)(virt_base + virt_size), (void *)phys_base, (void *)out->entry);
    return 0;
}
