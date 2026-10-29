/*
 * ProotOS — UEFI bootloader entry point.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "loader.h"

struct gop_info {
    uint64_t fb_phys;
    uint64_t fb_size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t format;
    int valid;
};
struct gop_info gop_init(int want_width, int want_height);

void console_use_uefi(int enabled);

static uint64_t find_rsdp(EFI_SYSTEM_TABLE *st) {
    if (!st->ConfigurationTable)
        return 0;
    for (UINTN i = 0; i < st->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *ct = &st->ConfigurationTable[i];
        if (!memcmp(&ct->VendorGuid, &EFI_ACPI_20_TABLE_GUID, sizeof(EFI_GUID)) ||
            !memcmp(&ct->VendorGuid, &EFI_ACPI_TABLE_GUID, sizeof(EFI_GUID))) {
            return (uint64_t)(uintptr_t)ct->VendorTable;
        }
    }
    return 0;
}

static EFI_STATUS read_memory_map(EFI_MEMORY_DESCRIPTOR **out_map, UINTN *out_size, UINTN *out_key,
                                  UINTN *out_desc_size, UINT32 *out_version) {
    UINTN size = 0, key = 0, desc_size = 0;
    UINT32 version = 0;
    EFI_STATUS st = gBS->GetMemoryMap(&size, 0, &key, &desc_size, &version);
    if (st != EFI_BUFFER_TOO_SMALL && EFI_ERROR(st)) {
        size = 4096;
    }
    size += 4096;

    for (int attempt = 0; attempt < 8; attempt++) {
        EFI_MEMORY_DESCRIPTOR *map = uefi_alloc_pool(size);
        st = gBS->GetMemoryMap(&size, map, &key, &desc_size, &version);
        if (!EFI_ERROR(st)) {
            *out_map = map;
            *out_size = size;
            *out_key = key;
            *out_desc_size = desc_size;
            *out_version = version;
            return EFI_SUCCESS;
        }
        gBS->FreePool(map);
        if (st != EFI_BUFFER_TOO_SMALL)
            return st;
        size += 4096;
    }
    return EFI_OUT_OF_RESOURCES;
}

static uint64_t max_physical_address(const EFI_MEMORY_DESCRIPTOR *map, UINTN size,
                                     UINTN desc_size) {
    uint64_t max = 0x100000000ULL;
    for (UINTN off = 0; off + desc_size <= size; off += desc_size) {
        const EFI_MEMORY_DESCRIPTOR *d =
            (const EFI_MEMORY_DESCRIPTOR *)((const uint8_t *)map + off);
        /* Only RAM-like ranges matter for the direct map. */
        if (!((d->Type >= 1 && d->Type <= 7) || d->Type == 9 || d->Type == 10 || d->Type == 14)) {
            continue;
        }
        uint64_t end = d->PhysicalStart + d->NumberOfPages * 4096ULL;
        if (end > max)
            max = end;
    }
    return max;
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image_handle, EFI_SYSTEM_TABLE *system_table) {
    gST = system_table;
    gBS = system_table->BootServices;
    gImageHandle = image_handle;

    if (gST->ConOut) {
        gST->ConOut->ClearScreen(gST->ConOut);
    }
    serial_init();
    console_printf("\n");
    console_printf("  ____            _    ___  ____\n");
    console_printf(" |  _ \\ _ __ ___ | |_ / _ \\/ ___|\n");
    console_printf(" | |_) | '__/ _ \\| __| | | \\___ \\\n");
    console_printf(" |  __/| | | (_) | |_| |_| |___) |\n");
    console_printf(" |_|   |_|  \\___/ \\__|\\___/|____/\n");
    console_printf(" ProotOS loader 0.0.1 (x86_64 UEFI)\n\n");

    gBS->SetWatchdogTimer(0, 0, 0, 0);

    struct boot_config cfg;
    config_defaults(&cfg);

    struct file_blob cfg_blob;
    if (fs_read_file(image_handle, "\\EFI\\BOOT\\PROOT.CFG", &cfg_blob) == 0) {
        config_parse(&cfg, (const char *)cfg_blob.data, cfg_blob.size);
        uefi_free_pages(cfg_blob.physical, cfg_blob.size ? cfg_blob.size : 1);
        console_printf("[loader] config loaded\n");
    } else {
        console_printf("[loader] no config, using defaults\n");
    }

    struct file_blob kernel_blob;
    if (fs_read_file(image_handle, cfg.kernel, &kernel_blob) != 0) {
        fail("cannot open kernel '%s'", cfg.kernel);
    }

    struct loaded_elf kernel;
    if (elf_load(&kernel_blob, &kernel) != 0) {
        fail("cannot load kernel '%s'", cfg.kernel);
    }
    uefi_free_pages(kernel_blob.physical, kernel_blob.size ? kernel_blob.size : 1);

    struct file_blob initramfs;
    memset(&initramfs, 0, sizeof(initramfs));
    if (fs_read_file(image_handle, cfg.initramfs, &initramfs) != 0) {
        console_printf("[loader] warning: no initramfs at '%s'\n", cfg.initramfs);
    } else {
        console_printf("[loader] initramfs: %llu bytes at %p\n", (unsigned long long)initramfs.size,
                       (void *)initramfs.physical);
    }

    struct gop_info gop = gop_init(cfg.video_width, cfg.video_height);
    if (gop.valid)
        console_use_uefi(0);

    uint64_t rsdp = find_rsdp(system_table);
    console_printf("[loader] ACPI RSDP: %p\n", (void *)rsdp);

    EFI_MEMORY_DESCRIPTOR *map = 0;
    UINTN map_size = 0, map_key = 0, desc_size = 0;
    UINT32 desc_version = 0;
    EFI_STATUS st = read_memory_map(&map, &map_size, &map_key, &desc_size, &desc_version);
    if (EFI_ERROR(st))
        fail("GetMemoryMap failed: %llx", (unsigned long long)st);

    uint64_t identity_limit = max_physical_address(map, map_size, desc_size);
    if (identity_limit < 0x100000000ULL)
        identity_limit = 0x100000000ULL;
    console_printf("[loader] memory map: %llu bytes, %llu descriptors, max phys %llu MiB\n",
                   (unsigned long long)map_size, (unsigned long long)(map_size / desc_size),
                   (unsigned long long)(identity_limit >> 20));
    gBS->FreePool(map);

    struct paging_result paging = paging_init(identity_limit, &kernel, gop.fb_phys, gop.fb_size);

    uint64_t bi_phys = uefi_alloc_pages(4096, 0xffffffffULL);
    struct proot_bootinfo *bi = (struct proot_bootinfo *)(uintptr_t)bi_phys;
    memset(bi, 0, 4096);
    bi->magic = PROOT_BOOTINFO_MAGIC;
    bi->version = PROOT_BOOTINFO_VERSION;
    bi->framebuffer_base = paging.fb_virt;
    bi->framebuffer_size = gop.fb_size;
    bi->framebuffer_width = gop.width;
    bi->framebuffer_height = gop.height;
    bi->framebuffer_pitch = gop.pitch;
    bi->framebuffer_bpp = gop.bpp;
    bi->framebuffer_format = gop.format;
    bi->kernel_base = kernel.virt_base;
    bi->kernel_size = kernel.virt_size;
    bi->kernel_phys = kernel.phys_base;
    bi->rsdp = rsdp;
    bi->initramfs_base = initramfs.physical;
    bi->initramfs_size = initramfs.size;
    strncpy(bi->cmdline, cfg.cmdline, sizeof(bi->cmdline) - 1);
    strncpy(bi->loader, "ProotOS loader 0.0.1", sizeof(bi->loader) - 1);

    /* Final map, then hand over to the kernel. */
    st = read_memory_map(&map, &map_size, &map_key, &desc_size, &desc_version);
    if (EFI_ERROR(st))
        fail("final GetMemoryMap failed: %llx", (unsigned long long)st);
    bi->memory_map = (uint64_t)(uintptr_t)map;
    bi->memory_map_size = map_size;
    bi->memory_map_desc_size = desc_size;

    console_printf("[loader] bootinfo=%p kernel entry=%p pml4=%p\n", (void *)bi_phys,
                   (void *)kernel.entry, (void *)paging.pml4_phys);
    console_printf("[loader] exiting boot services...\n");

    st = gBS->ExitBootServices(image_handle, map_key);
    if (EFI_ERROR(st)) {
        /* The map key can change if allocations happened; retry once. */
        EFI_MEMORY_DESCRIPTOR *map2 = 0;
        UINTN size2 = 0, key2 = 0, ds2 = 0;
        UINT32 dv2 = 0;
        EFI_STATUS st2 = read_memory_map(&map2, &size2, &key2, &ds2, &dv2);
        if (EFI_ERROR(st2))
            fail("ExitBootServices retry map failed: %llx", (unsigned long long)st2);
        bi->memory_map = (uint64_t)(uintptr_t)map2;
        bi->memory_map_size = size2;
        bi->memory_map_desc_size = ds2;
        st = gBS->ExitBootServices(image_handle, key2);
        if (EFI_ERROR(st))
            fail("ExitBootServices failed: %llx", (unsigned long long)st);
    }

    console_use_uefi(0);
    console_printf("[loader] boot services exited, jumping to kernel\n");
    jump_to_kernel(kernel.entry, PROOT_HHDM_BASE + bi_phys, paging.pml4_phys);
}
