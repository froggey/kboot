/*
 * Copyright (C) 2016 Henry Harrington
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
 * @brief               Mezzano loader internal definitions.
 */

#ifndef __LOADER_MEZZANO_H
#define __LOADER_MEZZANO_H

#include <compiler.h>
#include <device.h>
#include <fs.h>
#include <mmu.h>
#include <types.h>

/** On-disk image header. */
typedef struct mezzano_header {
    uint8_t magic[16];               /* +0 */
    uint8_t uuid[16];                /* +16 */
    uint16_t protocol_major;         /* +32 */
    uint16_t protocol_minor;         /* +34 */
    uint32_t _pad1;                  /* +36 */
    uint64_t entry_fref;             /* +40 */
    uint64_t initial_process;        /* +48 */
    uint64_t nil;                    /* +56 */
    uint8_t architecture;            /* +64 */
    uint8_t _pad2[7];                /* +65 */
    uint64_t initial_stack_pointer;  /* +72 */
    uint8_t _pad3[16];               /* +80 */
    uint64_t bml4;                   /* +96 */
    uint64_t freelist_head;          /* +104 */
} __packed mezzano_header_t;

enum architecture {
    arch_x86_64 = 1,
    arch_arm64 = 2,
};

enum page_type {
    page_type_other = 0,
    page_type_free = 1,
    page_type_wired = 2,
    page_type_wired_backing = 3,
    page_type_active = 4,
    page_type_active_writeback = 5,
    page_type_inactive_writeback = 6,
    page_type_page_table = 7
};

typedef struct mezzano_page_info {
    uint64_t flags;
    uint64_t extra;
    uint64_t next;
    uint64_t prev;
} __packed mezzano_page_info_t;

typedef struct mezzano_buddy_bin {
    uint64_t first_page;
    uint64_t count;
} __packed mezzano_buddy_bin_t;

typedef struct mezzano_memory_map_entry {
    uint64_t start;
    uint64_t end;
} __packed mezzano_memory_map_entry_t;

/* Boot info page */

typedef struct mezzano_video_information {
    // Framebuffer size is stride * height, aligned up to page size.
    uint64_t framebuffer_physical_address;                     //  +0 fixnum.
    uint64_t framebuffer_width;                                //  +8 fixnum, pixels.
    uint64_t framebuffer_pitch;                                // +16 fixnum, bytes.
    uint64_t framebuffer_height;                               // +24 fixnum, pixels.
    uint64_t framebuffer_layout;                               // +32 fixnum.
} __packed mezzano_video_information_t;

// Common(ish) layouts. Layouts beyond 32-bit XRGB will be supported in later boot protocols.
#define FRAMEBUFFER_LAYOUT_X8_R8_G8_B8 1  // 32-bit XRGB
//#define FRAMEBUFFER_LAYOUT_R8_G8_B8_X8 2  // 32-bit RGBX
//#define FRAMEBUFFER_LAYOUT_X8_B8_G8_R8 3  // 32-bit XBGR
//#define FRAMEBUFFER_LAYOUT_B8_G8_R8_X8 4  // 32-bit BGRX
#define FRAMEBUFFER_LAYOUT_X0_R8_G8_B8 5  // 24-bit RGB
//#define FRAMEBUFFER_LAYOUT_X0_B8_G8_R8 6  // 24-bit BGR
//#define FRAMEBUFFER_LAYOUT_X0_R5_G6_B5 7  // 16-bit 565 RGB
//#define FRAMEBUFFER_LAYOUT_X0_B5_G6_R5 8  // 16-bit 565 BGR
//#define FRAMEBUFFER_LAYOUT_X1_R5_G5_B5 9  // 16-bit 555 XRGB
//#define FRAMEBUFFER_LAYOUT_X1_B5_G5_R5 10 // 16-bit 555 XBGR
//#define FRAMEBUFFER_LAYOUT_R5_G5_B5_X1 11 // 16-bit 555 RGBX
//#define FRAMEBUFFER_LAYOUT_B5_G5_R5_X1 12 // 16-bit 555 BGRX

#define log2_4k_page 12
// Modifying these will change the layout of the boot info page.
// Changes the protocol version to be bumped.
// 32 bits - log2(4k_page_size)
#define mezzano_n_buddy_bins_32_bit (32-log2_4k_page)
// The physical map covers 512GB, log2 is 39.
#define mezzano_n_buddy_bins_64_bit (39-log2_4k_page)
#define mezzano_max_memory_map_size 32

typedef struct mezzano_boot_information {
    uint8_t uuid[16];                                              // +0 octets.
    // Buddy allocator for memory below 4GB.
    mezzano_buddy_bin_t buddy_bin_32[mezzano_n_buddy_bins_32_bit]; // +16
    // Buddy allocator for remaining memory.
    mezzano_buddy_bin_t buddy_bin_64[mezzano_n_buddy_bins_64_bit]; // +336
    // Video information.
    mezzano_video_information_t video;                             // +768
    uint64_t acpi_rsdp;                                            // +808
    uint64_t boot_options;                                         // +816
    // The memory map specifies where RAM exists, not what it can be used for.
    // If it's in the memory map, then it has an info struct mapped.
    // This is sorted in address order, with no overlaps.
    uint64_t n_memory_map_entries;                                 // +824 unsigned-byte 64.
    mezzano_memory_map_entry_t memory_map[mezzano_max_memory_map_size];
    uint64_t efi_system_table;                                     // +1344
    uint64_t fdt_address;                                          // +1352
    uint64_t block_map_address;                                    // +1360
} __packed mezzano_boot_information_t;

#define BOOT_OPTION_FORCE_READ_ONLY 0x01
#define BOOT_OPTION_FREESTANDING    0x02
#define BOOT_OPTION_VIDEO_CONSOLE   0x04
#define BOOT_OPTION_NO_DETECT       0x08
#define BOOT_OPTION_NO_SMP          0x10

#define BLOCK_MAP_PRESENT 0x01
#define BLOCK_MAP_WRITABLE 0x02
#define BLOCK_MAP_ZERO_FILL 0x04
#define BLOCK_MAP_WIRED 0x10
#define BLOCK_MAP_TRACK_DIRTY 0x20
#define BLOCK_MAP_TRANSIENT 0x40
#define BLOCK_MAP_FLAG_MASK 0xFF
#define BLOCK_MAP_ID_SHIFT 8

/** Structure containing Mezzano image loader state. */
typedef struct mezzano_loader {
    mezzano_header_t header;

    device_t *disk;            /**< Image device. */
    fs_handle_t *fs_handle;    /**< Image file. */
    char *device_name;        /**< Image device name. */
    bool force_ro;
    bool freestanding;
    bool video_console;
    bool no_detect;
    bool no_smp;
    uint64_t page_count;
    uint64_t n_pages_loaded;
    bool verbose;
} mezzano_loader_t;

// FIXME: Duplicated in enter.S
static const uint64_t mezzano_physical_map_address = 0xFFFF800000000000ull;
static const uint64_t mezzano_physical_info_address = 0xFFFF808000000000ull;
static const uint64_t mezzano_physical_map_size = 0x8000000000ull;

/// Convert val to a fixnum.
static inline uint64_t fixnum(int64_t val) {
    return val << 1;
}
// And the other way.
static inline int64_t unfixnum(uint64_t fix) {
    return ((int64_t)fix) >> 1;
}

extern void __noreturn mezzano_arch_enter(
    mmu_context_t *transition_pml4,
    mmu_context_t *pml4,
    uint64_t entry_fref,
    uint64_t initial_process,
    uint64_t boot_information_location,
    uint64_t nil,
    uint64_t initial_stack_pointer);

extern void mezzano_platform_load(mezzano_boot_information_t *boot_info);
extern void mezzano_platform_finalize(mezzano_boot_information_t *boot_info);
extern void mezzano_generate_memory_map(mezzano_loader_t *loader, mmu_context_t *mmu, mezzano_boot_information_t *boot_info);
extern void mezzano_set_video_mode(mezzano_boot_information_t *boot_info);

extern void mezzano_add_physical_memory_range(mezzano_loader_t *loader, mmu_context_t *mmu, mezzano_boot_information_t *boot_info, phys_ptr_t orig_start, phys_ptr_t orig_end, int cache_type);

#endif
