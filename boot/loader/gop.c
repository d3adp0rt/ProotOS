/*
 * ProotOS — UEFI GOP setup.
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

struct gop_info gop_init(int want_width, int want_height) {
    struct gop_info out = {0};

    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS st =
        gBS->LocateProtocol((EFI_GUID *)&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, 0, (VOID **)&gop);
    if (EFI_ERROR(st) || !gop) {
        console_printf("[loader] GOP unavailable: %llx\n", (unsigned long long)st);
        return out;
    }

    UINT32 chosen = gop->Mode ? gop->Mode->Mode : 0;
    for (UINT32 i = 0; i < gop->Mode->MaxMode; i++) {
        UINTN info_size = 0;
        EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = 0;
        st = gop->QueryMode(gop, i, &info_size, &info);
        if (EFI_ERROR(st) || !info)
            continue;
        if ((int)info->HorizontalResolution == want_width &&
            (int)info->VerticalResolution == want_height && info->PixelFormat != PixelBltOnly) {
            chosen = i;
            break;
        }
        if (info->PixelFormat != PixelBltOnly && info->HorizontalResolution >= 800 &&
            info->VerticalResolution >= 600) {
            chosen = i;
        }
    }

    st = gop->SetMode(gop, chosen);
    if (EFI_ERROR(st)) {
        console_printf("[loader] GOP SetMode(%u) failed: %llx\n", chosen, (unsigned long long)st);
        return out;
    }

    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info = gop->Mode->Info;
    out.fb_phys = gop->Mode->FrameBufferBase;
    out.fb_size = gop->Mode->FrameBufferSize;
    out.width = info->HorizontalResolution;
    out.height = info->VerticalResolution;
    out.pitch = info->PixelsPerScanLine * 4;
    out.bpp = 32;
    switch (info->PixelFormat) {
    case PixelRedGreenBlueReserved8BitPerColor:
        out.format = PROOT_FB_FORMAT_RGB;
        break;
    case PixelBlueGreenRedReserved8BitPerColor:
        out.format = PROOT_FB_FORMAT_BGR;
        break;
    case PixelBitMask:
        out.format = PROOT_FB_FORMAT_BITMASK;
        break;
    default:
        out.format = PROOT_FB_FORMAT_BGR;
        break;
    }
    out.valid = 1;

    console_printf("[loader] GOP: %ux%u pitch=%u phys=%p size=%llu fmt=%u\n", out.width, out.height,
                   out.pitch, (void *)out.fb_phys, (unsigned long long)out.fb_size, out.format);
    return out;
}
