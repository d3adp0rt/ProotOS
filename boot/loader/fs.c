/*
 * ProotOS — UEFI loader file access.
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "loader.h"

static void ascii_to_utf16(const char *src, CHAR16 *dst, size_t cap) {
    size_t i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = (CHAR16)(unsigned char)src[i];
        i++;
    }
    dst[i] = 0;
}

static EFI_STATUS open_root(EFI_HANDLE image_handle, EFI_FILE_PROTOCOL **root_out) {
    EFI_LOADED_IMAGE_PROTOCOL *li = 0;
    EFI_STATUS st = gBS->HandleProtocol(image_handle, (EFI_GUID *)&EFI_LOADED_IMAGE_PROTOCOL_GUID,
                                        (VOID **)&li);
    if (EFI_ERROR(st) || !li)
        return st;

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    st = gBS->HandleProtocol(li->DeviceHandle, (EFI_GUID *)&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
                             (VOID **)&fs);
    if (EFI_ERROR(st) || !fs)
        return st;

    return fs->OpenVolume(fs, root_out);
}

int fs_read_file(EFI_HANDLE image_handle, const char *path, struct file_blob *out) {
    memset(out, 0, sizeof(*out));

    EFI_FILE_PROTOCOL *root = 0;
    EFI_STATUS st = open_root(image_handle, &root);
    if (EFI_ERROR(st) || !root) {
        console_printf("[loader] no simple filesystem: %llx\n", (unsigned long long)st);
        return -1;
    }

    CHAR16 path16[256];
    ascii_to_utf16(path, path16, sizeof(path16) / sizeof(path16[0]));

    EFI_FILE_PROTOCOL *file = 0;
    st = root->Open(root, &file, path16, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st) || !file) {
        root->Close(root);
        return -1;
    }

    UINTN info_size = 0;
    st = file->GetInfo(file, (EFI_GUID *)&EFI_FILE_INFO_ID, &info_size, 0);
    if (st != EFI_BUFFER_TOO_SMALL && EFI_ERROR(st)) {
        file->Close(file);
        root->Close(root);
        return -1;
    }
    if (info_size < sizeof(EFI_FILE_INFO))
        info_size = sizeof(EFI_FILE_INFO) + 256;

    EFI_FILE_INFO *info = uefi_alloc_pool(info_size);
    st = file->GetInfo(file, (EFI_GUID *)&EFI_FILE_INFO_ID, &info_size, info);
    if (EFI_ERROR(st)) {
        file->Close(file);
        root->Close(root);
        return -1;
    }

    uint64_t size = info->FileSize;
    uint64_t phys = uefi_alloc_pages(size ? size : 1, 0xffffffffULL);
    gBS->FreePool(info);
    UINTN read_size = (UINTN)size;
    st = file->Read(file, &read_size, (VOID *)(uintptr_t)phys);
    file->Close(file);
    root->Close(root);

    if (EFI_ERROR(st)) {
        uefi_free_pages(phys, size ? size : 1);
        return -1;
    }
    if (read_size != size)
        size = read_size;

    out->data = (void *)(uintptr_t)phys;
    out->physical = phys;
    out->size = size;
    return 0;
}
