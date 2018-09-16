/*
 * Copyright (C) 2014-2017 Henry Harrington
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * @file
 * @brief               generic-arm64 platform Mezzano loader functions.
 */

#include <loader/mezzano.h>

#include <lib/string.h>
#include <lib/utility.h>

#include <generic-arm64/generic-arm64.h>

#include <assert.h>
#include <device.h>
#include <disk.h>
#include <loader.h>
#include <memory.h>
#include <mmu.h>
#include <ui.h>
#include <video.h>

void mezzano_generate_memory_map(mezzano_loader_t *loader, mmu_context_t *mmu, mezzano_boot_information_t *boot_info) {
    // FIXME: Need to figure this out from the FDT.

    // 0-1GB contains all the useful MMIO addresses.
    mezzano_add_physical_memory_range(loader, mmu, boot_info, 0x00000000, 0x40000000, MMU_CACHE_UNCACHED);

    // 1GB upwards is real memory.
    // FIXME: The framebuffer is part of real memory and should probably be mapped writethrough.
    size_t total_memory = orange_pi_pc2_total_memory();
    mezzano_add_physical_memory_range(loader, mmu, boot_info, 0x40000000, 0x40000000 + total_memory, MMU_CACHE_NORMAL);
}

void mezzano_set_video_mode(mezzano_boot_information_t *boot_info)
{
    if(current_video_mode) {
        boot_info->video.framebuffer_physical_address = fixnum(current_video_mode->mem_phys);
        boot_info->video.framebuffer_width = fixnum(current_video_mode->width);
        boot_info->video.framebuffer_height = fixnum(current_video_mode->height);
        boot_info->video.framebuffer_pitch = fixnum(current_video_mode->pitch);
        // FIXME.
        boot_info->video.framebuffer_layout = fixnum(FRAMEBUFFER_LAYOUT_X8_R8_G8_B8);
    } else {
        boot_info->video.framebuffer_physical_address = fixnum(0);
        boot_info->video.framebuffer_width = fixnum(0);
        boot_info->video.framebuffer_height = fixnum(0);
        boot_info->video.framebuffer_pitch = fixnum(4);
        boot_info->video.framebuffer_layout = fixnum(FRAMEBUFFER_LAYOUT_X8_R8_G8_B8);
    }
}

void mezzano_platform_load(mezzano_boot_information_t *boot_info) {
    boot_info->fdt_address = (uint64_t)fdt_address;
}

void mezzano_platform_finalize(mezzano_boot_information_t *boot_info) {
}
