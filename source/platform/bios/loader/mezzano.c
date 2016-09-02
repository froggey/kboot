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

#include <bios/bios.h>
#include <bios/memory.h>

#include <assert.h>
#include <device.h>
#include <disk.h>
#include <loader.h>
#include <memory.h>
#include <mmu.h>
#include <ui.h>
#include <video.h>

void mezzano_generate_memory_map(mmu_context_t *mmu, mezzano_boot_information_t *boot_info) {
    // Iterate the E820 memory map to generate the physical mapping region.
    // Only memory mentioned by the E820 map is mapped here. Device memory, etc is
    // left for the OS to detect and map.
    // TODO, eventually the system will need to know about ACPI reclaim/NVS areas.

    void *buf __cleanup_free;
    size_t num_entries, entry_size;

    bios_memory_get_mmap(&buf, &num_entries, &entry_size);
    for(size_t i = 0; i < num_entries; i++) {
        e820_entry_t *entry = buf + (i * entry_size);
        mezzano_add_physical_memory_range(mmu, boot_info, entry->start, entry->start + entry->length);
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
    default:
        return 0;
    }
}

void mezzano_set_video_mode(mezzano_boot_information_t *boot_info)
{
    video_mode_t *mode;
    int layout;

    mode = video_env_set(current_environ, "video_mode");
    if(!mode) {
        boot_error("Unable to find supported video mode.");
    }

    layout = determine_vbe_mode_layout(mode);
    if(!mode) {
        boot_error("Unable to find supported video mode.");
    }

    dprintf("mezzano: Using %ix%i video mode, layout %i, pitch %i, fb at %08x\n",
        mode->width, mode->height, layout, mode->pitch, mode->mem_phys);
    boot_info->video.framebuffer_physical_address = fixnum(mode->mem_phys);
    boot_info->video.framebuffer_width = fixnum(mode->width);
    boot_info->video.framebuffer_pitch = fixnum(mode->pitch);
    boot_info->video.framebuffer_height = fixnum(mode->height);
    boot_info->video.framebuffer_layout = fixnum(layout);
}

static bool acpi_checksum_range(phys_ptr_t start, unsigned int size) {
    uint8_t sum = 0;
    uint8_t *data = (uint8_t *)(ptr_t)start; // x86 uses a 1:1 p/v mapping.

    for(unsigned int i = 0; i < size; i++) {
        sum += data[i];
    }

    return sum == 0;
}

static uint64_t acpi_detect_range(phys_ptr_t start, phys_ptr_t end) {
    for(phys_ptr_t i = start; i < end; i += 16) {
        if(memcmp("RSD PTR ", (char *)(ptr_t)i, 8) == 0 && acpi_checksum_range(i, 20)) {
            return i;
        }
    }

    return 0;
}

// Return the physical address of the ACPI RSDP, or 0 if none was found.
static uint64_t acpi_detect(void) {
    uint64_t rsdp;

    /* OSPM finds the Root System Description Pointer (RSDP) structure by
     * searching physical memory ranges on 16-byte boundaries for a valid
     * Root System Description Pointer structure signature and checksum match
     * as follows:
     *   The first 1 KB of the Extended BIOS Data Area (EBDA).
     *   The BIOS read-only memory space between 0E0000h and 0FFFFFh. */

    /* Search the EBDA */
    phys_ptr_t ebda_start = ((phys_ptr_t)*(uint16_t *)0x40E) << 4;
    rsdp = acpi_detect_range(ebda_start, ebda_start + 1024);
    if(rsdp) {
        dprintf("Detect ACPI RSDP at %016" PRIx64 " via the EBDA.\n", rsdp);
        return rsdp;
    }

    /* Search the BIOS ROM */
    rsdp = acpi_detect_range(0xE0000, 0x100000);
    if(rsdp) {
        dprintf("Detect ACPI RSDP at %016" PRIx64 " via the BIOS ROM.\n", rsdp);
        return rsdp;
    }

    dprintf("Failed to detect ACPI RSDP.\n");
    return 0;
}

void mezzano_platform_load(mezzano_boot_information_t *boot_info) {
    boot_info->acpi_rsdp = acpi_detect();
}
