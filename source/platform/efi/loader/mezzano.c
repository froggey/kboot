/*
 * Copyright (C) 2014-2016 Henry Harrington
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
 * @brief               BIOS platform Mezzano loader functions.
 */

#include <loader/mezzano.h>

#include <lib/string.h>
#include <lib/utility.h>

#include <efi/efi.h>
#include <efi/services.h>

#include <assert.h>
#include <device.h>
#include <disk.h>
#include <loader.h>
#include <memory.h>
#include <mmu.h>
#include <ui.h>
#include <video.h>

void mezzano_generate_memory_map(mmu_context_t *mmu, mezzano_boot_information_t *boot_info) {
    efi_memory_descriptor_t *memory_map __cleanup_free = NULL;
    efi_uintn_t num_entries, map_key;
    efi_status_t ret;

    ret = efi_get_memory_map(&memory_map, &num_entries, &map_key);
    if (ret != EFI_SUCCESS) {
        internal_error("Failed to get memory map (0x%zx)", ret);
    }

    for(efi_uintn_t i = 0; i < num_entries; i += 1) {
        mezzano_add_physical_memory_range(mmu, boot_info, memory_map[i].physical_start, memory_map[i].physical_start + memory_map[i].num_pages * EFI_PAGE_SIZE);
    }
}

static int determine_vbe_mode_layout(video_mode_t *mode) {
    if(mode->type != VIDEO_MODE_LFB) {
        return 0;
    }
    switch(mode->format.bpp) {
    case 32:
        if(mode->format.red_size == 8 &&
           mode->format.red_pos == 16 &&
           mode->format.green_size == 8 &&
           mode->format.green_pos == 8 &&
           mode->format.blue_size == 8 &&
           mode->format.blue_pos == 0) {
            return FRAMEBUFFER_LAYOUT_X8_R8_G8_B8;
        } else {
            return 0;
        }
    case 24:
        if(mode->format.red_size == 8 &&
           mode->format.red_pos == 16 &&
           mode->format.green_size == 8 &&
           mode->format.green_pos == 8 &&
           mode->format.blue_size == 8 &&
           mode->format.blue_pos == 0) {
            return FRAMEBUFFER_LAYOUT_X0_R8_G8_B8;
        } else {
            return 0;
        }
    default:
        return 0;
    }
}

void mezzano_set_video_mode(mezzano_boot_information_t *boot_info) {
    video_mode_t *mode;
    int layout;

    mode = current_video_mode;

    layout = determine_vbe_mode_layout(mode);
    if(!layout) {
        boot_error("Selected video mode is not supported. Type %i, bpp %i r%i-%i g%i-%i b%i-%i",
                   mode->type, mode->format.bpp,
                   mode->format.red_size, mode->format.red_pos,
                   mode->format.green_size, mode->format.green_pos,
                   mode->format.blue_size, mode->format.blue_pos);
    }

    dprintf("mezzano: Using %ix%i video mode, layout %i, pitch %i, fb at %08x\n",
        mode->width, mode->height, layout, mode->pitch, mode->mem_phys);
    boot_info->video.framebuffer_physical_address = fixnum(mode->mem_phys);
    boot_info->video.framebuffer_width = fixnum(mode->width);
    boot_info->video.framebuffer_pitch = fixnum(mode->pitch);
    boot_info->video.framebuffer_height = fixnum(mode->height);
    boot_info->video.framebuffer_layout = fixnum(layout);
}

void mezzano_platform_load(mezzano_boot_information_t *boot_info) {
}

void mezzano_platform_finalize(mezzano_boot_information_t *boot_info) {
    void *memory_map __cleanup_free;
    efi_uintn_t num_entries, desc_size;
    efi_uint32_t desc_version;

    /* Exit boot services mode and get the final memory map. */
    efi_exit_boot_services(&memory_map, &num_entries, &desc_size, &desc_version);

    boot_info->efi_system_table = virt_to_phys((ptr_t)efi_system_table);
}
