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
 * @brief        Mezzano loader.
 *
 * This file implements the 'mezzano' command for loading a Mezzano image.
 *
 * The 'mezzano' command is used as follows:
 *
 *   mezzano "<device name>"
 */

#include <loader/mezzano.h>

#include <lib/string.h>
#include <lib/utility.h>

#include <assert.h>
#include <device.h>
#include <disk.h>
#include <loader.h>
#include <memory.h>
#include <mmu.h>
#include <ui.h>
#include <video.h>

static const char mezzano_magic[] = "\x00MezzanineImage\x00";
static const uint16_t mezzano_protocol_major = 0;
static const uint16_t mezzano_protocol_minor = 26;

static void mprintf(const char *fmt, ...) {
    va_list args;

    va_start(args, fmt);
    dvprintf(fmt, args);
    va_end(args);
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
}

static bool in_range(uint64_t start, uint64_t end, uint64_t value) {
    return start <= value && value <= end;
}

static void crunch_memory_map(mezzano_boot_information_t *boot_info) {
    // Merge all overlapping/contiguous regions in the memory map.
    // insert_into_memory_map may produce opportunities for merging.

    // TODO.
}

static void mezzano_insert_into_memory_map(mezzano_boot_information_t *boot_info, uint64_t start, uint64_t end) {
    // Search for a existing region to merge with/insert after.
    uint64_t i = 0;
    for(; i < boot_info->n_memory_map_entries; i += 1) {
        if(boot_info->memory_map[i].start > end) {
            // Insert before this entry.
            break;
        }
        // Merge entries when the new region overlaps.
        if(in_range(boot_info->memory_map[i].start, boot_info->memory_map[i].end, start) ||
           in_range(boot_info->memory_map[i].start, boot_info->memory_map[i].end, end)) {
            if(boot_info->memory_map[i].start > start) {
                boot_info->memory_map[i].start = start;
            }
            if(boot_info->memory_map[i].end < end) {
                boot_info->memory_map[i].end = end;
            }
            crunch_memory_map(boot_info);
            return;
        }
    }
    // Can't merge with an existing entry. Insert a new region.
    if(boot_info->n_memory_map_entries == mezzano_max_memory_map_size) {
        dprintf("Too many memory map entries. Ignoring %016" PRIx64 "-%016" PRIx64 "\n",
                start, end);
        return;
    }
    // Shuffle entries up.
    memmove(&boot_info->memory_map[i+1], &boot_info->memory_map[i], (boot_info->n_memory_map_entries - i) * sizeof(mezzano_memory_map_entry_t));
    // Set.
    boot_info->memory_map[i].start = start;
    boot_info->memory_map[i].end = end;
    boot_info->n_memory_map_entries += 1;

    crunch_memory_map(boot_info);
}

void mezzano_add_physical_memory_range(mezzano_loader_t *loader, mmu_context_t *mmu, mezzano_boot_information_t *boot_info, phys_ptr_t orig_start, phys_ptr_t orig_end, int cache_type) {
    // Map liberally, it doesn't matter if free regions overlap with allocated
    // regions. It's more important that the entire region is mapped.
    phys_ptr_t start = round_down(orig_start, PAGE_SIZE);
    phys_ptr_t end = round_up(orig_end, PAGE_SIZE);

    // Ignore this region if exceeds the map area limit.
    if(start >= mezzano_physical_map_size) {
        return;
    }

    // Trim it if it starts below the limit.
    if(end > mezzano_physical_map_size) {
        end = mezzano_physical_map_size;
    }

    /* What we did above may have made the region too small,
     * ignore it if this is the case. */
    if(end <= start) {
        return;
    }

    if(loader->verbose) {
        mprintf("mezzano: Map physical memory region %016" PRIx64 "-%016" PRIx64 " %016" PRIx64 "-%016" PRIx64 "\n",
                orig_start, orig_end, start, end);
    }

    // Map the memory into the physical map region.
    mmu_map(mmu, mezzano_physical_map_address + start, start, end - start, cache_type, true);

    // Carefully insert it into the memory map, maintaining the sortedness.
    mezzano_insert_into_memory_map(boot_info, start, end);
}

static void mezzano_finalize_memory_map(mezzano_loader_t *loader, mmu_context_t *mmu, mezzano_boot_information_t *boot_info) {
    if(loader->verbose) {
        mprintf("mezzano: Final memory map:\n");
        for(uint64_t i = 0; i < boot_info->n_memory_map_entries; i += 1) {
            mprintf("  %016" PRIx64 "-%016" PRIx64 "\n",
                    boot_info->memory_map[i].start,
                    boot_info->memory_map[i].end);
        }
    }

    // Allocate the information structs for all the pages in the memory map.
    // FIXME: This has a bit of a problem with overlap, leaking a few pages.
    for(uint64_t i = 0; i < boot_info->n_memory_map_entries; i += 1) {
        phys_ptr_t start = boot_info->memory_map[i].start;
        phys_ptr_t end = boot_info->memory_map[i].end;
        phys_ptr_t info_start = round_down((mezzano_physical_info_address + (start / PAGE_SIZE) * sizeof(mezzano_page_info_t)), PAGE_SIZE);
        phys_ptr_t info_end = round_up((mezzano_physical_info_address + (end / PAGE_SIZE) * sizeof(mezzano_page_info_t)), PAGE_SIZE);
        phys_ptr_t phys_info_addr;
        if(loader->verbose) {
            mprintf("mezzano: info range %016" PRIx64 "-%016" PRIx64 "\n", info_start, info_end);
        }
        // FIXME/TODO: It's ok for the backing pages to be discontinuous.
        // Could use 2MB pages here as well.
        void *virt = memory_alloc(info_end - info_start, // size
                      0x1000, // alignment
                      0x100000, 0, // min/max address
                      MEMORY_TYPE_ALLOCATED, // type
                      0, // flags
                      &phys_info_addr);
        mmu_map(mmu, info_start, phys_info_addr, info_end - info_start, MMU_CACHE_NORMAL, true);
        memset(virt, 0, info_end - info_start);
    }
}

static uint64_t page_info_flags(mmu_context_t *mmu, phys_ptr_t page) {
    uint64_t offset = page / PAGE_SIZE;
    uint64_t result;
    mmu_memcpy_from(mmu, &result, mezzano_physical_info_address + offset * sizeof(mezzano_page_info_t) + offsetof(mezzano_page_info_t, flags), sizeof result);
    return result;
}

static void set_page_info_flags(mmu_context_t *mmu, phys_ptr_t page, uint64_t value) {
    uint64_t offset = page / PAGE_SIZE;
    mmu_memcpy_to(mmu, mezzano_physical_info_address + offset * sizeof(mezzano_page_info_t) + offsetof(mezzano_page_info_t, flags), &value, sizeof value);
}

static enum page_type page_info_type(mmu_context_t *mmu, phys_ptr_t page) {
    return unfixnum(page_info_flags(mmu, page)) & 0xFF;
}

static void set_page_info_type(mmu_context_t *mmu, phys_ptr_t page, enum page_type value) {
    uint64_t flags = unfixnum(page_info_flags(mmu, page));
    flags &= ~0xFF;
    flags |= value;
    set_page_info_flags(mmu, page, fixnum(flags));
}

static uint8_t page_info_bin(mmu_context_t *mmu, phys_ptr_t page) {
    return (unfixnum(page_info_flags(mmu, page)) >> 8) & 0xFF;
}

static void set_page_info_bin(mmu_context_t *mmu, phys_ptr_t page, uint8_t value) {
    uint64_t flags = unfixnum(page_info_flags(mmu, page));
    flags &= ~(0xFF << 8);
    flags |= value << 8;
    set_page_info_flags(mmu, page, fixnum(flags));
}

/*
static uint64_t page_info_extra(mmu_context_t *mmu, phys_ptr_t page) {
    uint64_t offset = page / PAGE_SIZE;
    uint64_t result;
    mmu_memcpy_from(mmu, &result, mezzano_physical_info_address + offset * sizeof(mezzano_page_info_t) + offsetof(mezzano_page_info_t, extra), sizeof result);
    return result;
}
*/

static void set_page_info_extra(mmu_context_t *mmu, phys_ptr_t page, uint64_t value) {
    uint64_t offset = page / PAGE_SIZE;
    mmu_memcpy_to(mmu, mezzano_physical_info_address + offset * sizeof(mezzano_page_info_t) + offsetof(mezzano_page_info_t, extra), &value, sizeof value);
}

static uint64_t page_info_next(mmu_context_t *mmu, phys_ptr_t page) {
    uint64_t offset = page / PAGE_SIZE;
    uint64_t result;
    mmu_memcpy_from(mmu, &result, mezzano_physical_info_address + offset * sizeof(mezzano_page_info_t) + offsetof(mezzano_page_info_t, next), sizeof result);
    return result;
}

static void set_page_info_next(mmu_context_t *mmu, phys_ptr_t page, uint64_t value) {
    uint64_t offset = page / PAGE_SIZE;
    mmu_memcpy_to(mmu, mezzano_physical_info_address + offset * sizeof(mezzano_page_info_t) + offsetof(mezzano_page_info_t, next), &value, sizeof value);
}

static uint64_t page_info_prev(mmu_context_t *mmu, phys_ptr_t page) {
    uint64_t offset = page / PAGE_SIZE;
    uint64_t result;
    mmu_memcpy_from(mmu, &result, mezzano_physical_info_address + offset * sizeof(mezzano_page_info_t) + offsetof(mezzano_page_info_t, prev), sizeof result);
    return result;
}

static void set_page_info_prev(mmu_context_t *mmu, phys_ptr_t page, uint64_t value) {
    uint64_t offset = page / PAGE_SIZE;
    mmu_memcpy_to(mmu, mezzano_physical_info_address + offset * sizeof(mezzano_page_info_t) + offsetof(mezzano_page_info_t, prev), &value, sizeof value);
}

static phys_ptr_t buddy(int k, phys_ptr_t x) {
    return x ^ ((phys_ptr_t)1 << (k + log2_4k_page));
}

static bool page_exists(mezzano_boot_information_t *boot_info, phys_ptr_t page) {
    for(uint64_t i = 0; i < boot_info->n_memory_map_entries; i += 1) {
        if(boot_info->memory_map[i].start <= page && page < boot_info->memory_map[i].end) {
            return true;
        }
    }
    return false;
}

static void buddy_free_page(mmu_context_t *mmu, mezzano_boot_information_t *boot_info, uint64_t nil, phys_ptr_t l) {
    //phys_ptr_t orig = l;
    mezzano_buddy_bin_t *buddies;
    int m;
    int k = 0;

    if(l < 0x100000000ull) { // 4GB
        m = mezzano_n_buddy_bins_32_bit - 1;
        buddies = boot_info->buddy_bin_32;
    } else {
        m = mezzano_n_buddy_bins_64_bit - 1;
        buddies = boot_info->buddy_bin_64;
    }

    //if(orig > 0x000000013FF00000ull)
    //    dprintf("Freeing page %016" PRIx64 "\n", l);

    while(1) {
        phys_ptr_t p = buddy(k, l);
        //if(orig > 0x000000013FF00000ull)
        //    dprintf(" buddy %016" PRIx64 "  order %i\n", p, k);

        if(k == m || // Stop trying to combine at the last bin.
           !page_exists(boot_info, p) || // Stop when the buddy doesn't actually exist.
           // Don't combine if buddy is not free
           page_info_type(mmu, p) != page_type_free ||
           // Only combine when the buddy is in this bin.
           (page_info_type(mmu, p) == page_type_free &&
            page_info_bin(mmu, p) != k)) {
            break;
        }
        // remove buddy from avail[k]
        if(buddies[k].first_page == fixnum(p / PAGE_SIZE)) {
            buddies[k].first_page = page_info_next(mmu, p);
        }
        if(page_info_next(mmu, p) != nil) {
            set_page_info_prev(mmu, unfixnum(page_info_next(mmu, p)) * PAGE_SIZE, page_info_prev(mmu, p));
        }
        if(page_info_prev(mmu, p) != nil) {
            set_page_info_next(mmu, unfixnum(page_info_prev(mmu, p)) * PAGE_SIZE, page_info_next(mmu, p));
        }
        buddies[k].count -= fixnum(1);
        k += 1;
        if(p < l) {
            l = p;
        }
    }
    //if(orig > 0x000000013FF00000ull)
    //    dprintf(" final %016" PRIx64 "\n", l);

    set_page_info_type(mmu, l, page_type_free);
    set_page_info_bin(mmu, l, k);
    set_page_info_next(mmu, l, buddies[k].first_page);
    set_page_info_prev(mmu, l, nil);
    if(buddies[k].first_page != nil) {
        set_page_info_prev(mmu, unfixnum(buddies[k].first_page) * PAGE_SIZE, fixnum(l / PAGE_SIZE));
    }
    buddies[k].first_page = fixnum(l / PAGE_SIZE);
    buddies[k].count += fixnum(1);
}

static uint64_t read_block_map_level(mezzano_loader_t *loader, uint64_t level_disk_block, int level) {
    phys_ptr_t phys_addr;
    void *data = memory_alloc(0x1000, // size
                              0x1000, // alignment
                              0x100000, 0, // min/max address
                              MEMORY_TYPE_ALLOCATED, // type
                              0, // flags
                              &phys_addr);

    void *disk_block;
    if(level != 1) {
        disk_block = malloc(0x1000);
    } else {
        disk_block = data;
    }

    status_t st;
    if(loader->disk) {
        st = device_read(loader->disk,
                         disk_block,
                         0x1000,
                         level_disk_block * 0x1000);
    } else {
        st = fs_read(loader->fs_handle,
                     disk_block,
                     0x1000,
                     level_disk_block * 0x1000);
    }
    if(st) {
        boot_error("Could not read block %" PRIu64 ": %pS", level_disk_block, st);
    }

    if(level == 1) {
        for(int i = 0; i < 512; i += 1) {
            uint64_t entry = ((uint64_t *)disk_block)[i];
            uint64_t id = entry >> BLOCK_MAP_ID_SHIFT;
            if(id != 0 && (entry & BLOCK_MAP_PRESENT)) {
                if(loader->freestanding) {
                    loader->page_count += 1;
                } else {
                    if(entry & BLOCK_MAP_WIRED) {
                        loader->page_count += 1;
                    }
                }
            }
        }
        return phys_addr + mezzano_physical_map_address;
    }


    for(int i = 0; i < 512; i += 1) {
        uint64_t entry = ((uint64_t *)disk_block)[i];
        uint64_t id = entry >> BLOCK_MAP_ID_SHIFT;
        if(id == 0) {
            ((uint64_t *)data)[i] = 0;
        } else {
            ((uint64_t *)data)[i] = read_block_map_level(loader, id, level - 1);
        }
    }

    free(disk_block);

    return phys_addr + mezzano_physical_map_address;
}

static void mezzano_read_block_map(mezzano_loader_t *loader, mezzano_boot_information_t *boot_info) {
    boot_info->block_map_address = read_block_map_level(loader, loader->header.bml4, 4);
}

typedef struct page_chunk {
    void *bootloader_virt;
    phys_ptr_t phys_addr;
    phys_size_t remaining;
} page_chunk_t;

// Allocate pages in 8MB chunks to reduce overall number of allocations, as large numbers
// of allocations can upset some EFI firmwares.
#define PAGE_CHUNK_SIZE (8 * 1024 * 1024)

static void load_page(mezzano_loader_t *loader, mmu_context_t *mmu, uint64_t info, uint64_t virtual, page_chunk_t *chunk) {
    if((info & BLOCK_MAP_PRESENT) == 0 || (info & BLOCK_MAP_TRANSIENT)) {
        return;
    }

    // Alloc phys.
    if(chunk->remaining == 0) {
        phys_size_t chunk_size = min(PAGE_CHUNK_SIZE, (loader->page_count - loader->n_pages_loaded) * PAGE_SIZE);
        chunk->bootloader_virt = memory_alloc(chunk_size, // size
                                              0x1000, // alignment
                                              0x100000, 0, // min/max address
                                              MEMORY_TYPE_ALLOCATED, // type
                                              0, // flags
                                              &chunk->phys_addr);
        chunk->remaining = chunk_size;
    }

    static_assert((PAGE_CHUNK_SIZE % PAGE_SIZE) == 0);
    phys_ptr_t phys_addr = chunk->phys_addr;
    void *bootloader_virt = chunk->bootloader_virt;
    chunk->bootloader_virt = (void*)((ptr_t)chunk->bootloader_virt + PAGE_SIZE);
    chunk->phys_addr += PAGE_SIZE;
    chunk->remaining -= PAGE_SIZE;

    // Map...
    // Writable only if writable and not doing dirty tracking.
    mmu_map(mmu, virtual, phys_addr, PAGE_SIZE, MMU_CACHE_NORMAL, (info & BLOCK_MAP_WRITABLE) && !(info & BLOCK_MAP_TRACK_DIRTY));
    // Write block number to page info struct.
    set_page_info_extra(mmu, phys_addr, fixnum(info >> BLOCK_MAP_ID_SHIFT));
    if(info & BLOCK_MAP_WIRED) {
        set_page_info_type(mmu, phys_addr, page_type_wired);
    } else {
        set_page_info_type(mmu, phys_addr, page_type_active);
    }

    if(info & BLOCK_MAP_ZERO_FILL) {
        memset(bootloader_virt, 0, PAGE_SIZE);
    } else {
        status_t st;
        if(loader->disk) {
            st = device_read(loader->disk,
                             bootloader_virt,
                             0x1000,
                             (info >> BLOCK_MAP_ID_SHIFT) * 0x1000);
        } else {
            st = fs_read(loader->fs_handle,
                         bootloader_virt,
                         0x1000,
                         (info >> BLOCK_MAP_ID_SHIFT) * 0x1000);
        }
        if(st) {
            boot_error("Could not read block %" PRIu64 " for virtual address %" PRIx64 ": %pS", info, virtual, st);
        }
    }

    loader->n_pages_loaded += 1;
    if((loader->n_pages_loaded % 100) == 0) {
        printf("%lli ", loader->n_pages_loaded);
    }
}

static void mezzano_read_wired_pages(mezzano_loader_t *loader, mmu_context_t *mmu, mezzano_boot_information_t *boot_info) {
    // Traverse the block map looking for wired pages.
    mprintf("Loading %lli %spages...\n", loader->page_count, loader->freestanding ? "" : "wired ");
    page_chunk_t chunk = {};
    uint64_t *bml4 = (uint64_t *)(ptr_t)(boot_info->block_map_address - mezzano_physical_map_address);
    for(int i = 0; i < 512; i += 1) {
        if(!bml4[i]) {
            continue;
        }
        uint64_t *bml3 = (uint64_t *)(ptr_t)(bml4[i] - mezzano_physical_map_address);
        for(int j = 0; j < 512; j += 1) {
            if(!bml3[j]) {
                continue;
            }
            uint64_t *bml2 = (uint64_t *)(ptr_t)(bml3[j] - mezzano_physical_map_address);
            for(int k = 0; k < 512; k += 1) {
                if(!bml2[k]) {
                    continue;
                }
                uint64_t *bml1 = (uint64_t *)(ptr_t)(bml2[k] - mezzano_physical_map_address);
                for(int l = 0; l < 512; l += 1) {
                    if(!loader->freestanding &&
                       (bml1[l] & BLOCK_MAP_WIRED) == 0) {
                        continue;
                    }
                    uint64_t address =
                        ((uint64_t)i << 39ull) |
                        ((uint64_t)j << 30ull) |
                        ((uint64_t)k << 21ull) |
                        ((uint64_t)l << 12ull);
                    load_page(loader, mmu, bml1[l], address, &chunk);
                }
            }
        }
    }
    mprintf("complete\n");
}

static void dump_one_buddy_allocator(mmu_context_t *mmu, mezzano_boot_information_t *boot_info, uint64_t nil, mezzano_buddy_bin_t *buddies, int max) {
    for(int k = 0; k < max; k += 1) {
        mprintf("Order %i %" PRIu64 " %016" PRIx64 ":\n", (k + log2_4k_page), buddies[k].count, buddies[k].first_page);
        uint64_t current = buddies[k].first_page;
        while(true) {
            if(current == nil) {
                break;
            }
            assert((current & 1) == 0);
            mprintf("  %016" PRIx64 "-%016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
                   unfixnum(current) * PAGE_SIZE,
                   unfixnum(current) * PAGE_SIZE + ((phys_ptr_t)1 << (12+k)),
                   page_info_next(mmu, unfixnum(current) * PAGE_SIZE),
                   page_info_prev(mmu, unfixnum(current) * PAGE_SIZE));
            current = page_info_next(mmu, unfixnum(current) * PAGE_SIZE);
        }
    }
}

static void dump_buddy_allocator(mmu_context_t *mmu, mezzano_boot_information_t *boot_info, uint64_t nil) {
    mprintf("32-bit buddy allocator:\n");
    dump_one_buddy_allocator(mmu, boot_info, nil, boot_info->buddy_bin_32, mezzano_n_buddy_bins_32_bit);
    mprintf("64-bit buddy allocator:\n");
    dump_one_buddy_allocator(mmu, boot_info, nil, boot_info->buddy_bin_64, mezzano_n_buddy_bins_64_bit);
}

/** Load the operating system. */
static __noreturn void mezzano_loader_load(void *_loader) {
    mezzano_loader_t *loader = _loader;
    mmu_context_t *mmu = mmu_context_create(LOAD_MODE_64BIT, MEMORY_TYPE_PAGETABLES);
    mmu_context_t *transition = mmu_context_create(LOAD_MODE_64BIT, MEMORY_TYPE_INTERNAL);

    // Allocate the boot info page.
    phys_ptr_t boot_info_page;
    void *boot_info_page_virt = memory_alloc(PAGE_SIZE, // size
                                             0x1000, // alignment
                                             0x100000, 0, // min/max address
                                             MEMORY_TYPE_ALLOCATED, // type
                                             0, // flags
                                             &boot_info_page);
    mezzano_boot_information_t *boot_info = boot_info_page_virt;
    memset(boot_info, 0, PAGE_SIZE);

    if(loader->force_ro) {
        boot_info->boot_options |= fixnum(BOOT_OPTION_FORCE_READ_ONLY);
    }
    if(loader->freestanding) {
        boot_info->boot_options |= fixnum(BOOT_OPTION_FREESTANDING);
    }
    if(loader->video_console) {
        boot_info->boot_options |= fixnum(BOOT_OPTION_VIDEO_CONSOLE);
    }
    if(loader->no_detect) {
        boot_info->boot_options |= fixnum(BOOT_OPTION_NO_DETECT);
    }
    if(loader->no_smp) {
        boot_info->boot_options |= fixnum(BOOT_OPTION_NO_SMP);
    }

    mezzano_generate_memory_map(loader, mmu, boot_info);
    mezzano_finalize_memory_map(loader, mmu, boot_info);

    mezzano_read_block_map(loader, boot_info);
    mezzano_read_wired_pages(loader, mmu, boot_info);

    mezzano_platform_load(boot_info);

    memcpy(boot_info->uuid, loader->header.uuid, 16);

    loader_preboot();

    mezzano_set_video_mode(boot_info);

    // Initialize buddy bins.
    for(int i = 0; i < mezzano_n_buddy_bins_32_bit; ++i) {
        boot_info->buddy_bin_32[i].first_page = loader->header.nil;
        boot_info->buddy_bin_32[i].count = fixnum(0);
    }
    for(int i = 0; i < mezzano_n_buddy_bins_64_bit; ++i) {
        boot_info->buddy_bin_64[i].first_page = loader->header.nil;
        boot_info->buddy_bin_64[i].count = fixnum(0);
    }

    // Generate the page tables used for transitioning from identity mapping
    // to the final page tables.
    // The loader must be identity mapped, and mapped in the physical region.
    ptr_t loader_start = round_down((ptr_t)__start, PAGE_SIZE);
    ptr_t loader_size = round_up((ptr_t)__end - (ptr_t)__start, PAGE_SIZE);
    mmu_map(transition, loader_start, loader_start, loader_size, MMU_CACHE_NORMAL, true);
    mmu_map(transition, mezzano_physical_map_address + loader_start, loader_start, loader_size, MMU_CACHE_NORMAL, true);
    mmu_map(mmu, mezzano_physical_map_address + loader_start, loader_start, loader_size, MMU_CACHE_NORMAL, true);

    /* Reclaim all memory used internally. */
    list_t kboot_memory_map;
    memory_finalize(&kboot_memory_map);
    if(loader->verbose) {
        dprintf("mezzano: final physical memory map:\n");
        memory_map_dump(&kboot_memory_map);
    }

    // For each free kboot memory region, add pages to the buddy allocator.
    // Also avoid any memory below 1MB, it's weird.
    // https://lkml.org/lkml/2013/11/11/614
    // fixme: do this in a less stupid way (whole power-of-two chunks at a time, not single pages).
    list_foreach(&kboot_memory_map, iter) {
        memory_range_t *range = list_entry(iter, memory_range_t, header);

        for(phys_ptr_t i = 0; i < range->size; i += PAGE_SIZE) {
            if(range->type == MEMORY_TYPE_FREE && range->start + i > 1024 * 1024) {
                buddy_free_page(mmu, boot_info, loader->header.nil, range->start + i);
            } else if(range->type == MEMORY_TYPE_PAGETABLES) {
                set_page_info_type(mmu, range->start + i, page_type_page_table);
            }
        }
    }

    if(loader->verbose) {
        dump_buddy_allocator(mmu, boot_info, loader->header.nil);
    }

    uint64_t entry_point, tsp;
    mmu_memcpy_from(mmu, &entry_point, loader->header.entry_fref + 15, sizeof entry_point);
    mmu_memcpy_from(mmu, &tsp, loader->header.initial_process + 31, sizeof tsp);

    mprintf("mezzano: Starting system. Entry point is %08" PRIx64 "  sp is %08" PRIx64 "  info is %08" PRIx64 "\n", entry_point, tsp, mezzano_physical_map_address + boot_info_page);
    mezzano_platform_finalize(boot_info);
    mezzano_arch_enter(transition,
                       mmu,
                       loader->header.entry_fref,
                       loader->header.initial_process,
                       fixnum(mezzano_physical_map_address + boot_info_page),
                       loader->header.nil,
                       loader->header.initial_stack_pointer);
}

#if CONFIG_KBOOT_UI

/** Return a window for configuring the OS.
 * @return        Pointer to configuration window. */
static ui_window_t *mezzano_loader_configure(void) {
    return NULL;
}

#endif

/** Mezzano loader type. */
static loader_ops_t mezzano_loader_ops = {
    .load = mezzano_loader_load,
#if CONFIG_KBOOT_UI
    .configure = mezzano_loader_configure,
#endif
};

static bool mezzano_locate_image(const char *path, device_t **_device, fs_handle_t **_fs_handle) {
    status_t st;

    if(strncmp(path, "uuid:", 5) == 0) {
        // Search all devices for an image with a matching UUID.
        const char *path_uuid = path + 5;
        list_foreach(&device_list, iter) {
            char uuid[UUID_STR_LEN];
            device_t *device = list_entry(iter, device_t, header);
            mezzano_header_t header;
            st = device_read(device, &header, sizeof(mezzano_header_t), 0);
            if(st) {
                dprintf("mezzano: Unable read device %s when searching for %s: %pS\n",
                        device->name, path, st);
                continue;
            }
            if(memcmp(header.magic, mezzano_magic, 16) != 0) {
                continue;
            }
            snprintf(uuid, sizeof uuid, "%pU", header.uuid);
            if(strcasecmp(uuid, path_uuid) == 0) {
                mprintf("mezzano: Detected UUID %pU on device %s.\n",
                        header.uuid, device->name);
                *_device = device;
                *_fs_handle = NULL;
                return true;
            }
        }
    }

    device_t *device = device_lookup(path);
    fs_handle_t *fs_handle = NULL;

    /* Verify the device exists and is compatible (disks only, currently). */
    if(device) {
        if(device->type != DEVICE_TYPE_DISK) {
            config_error("mezzano: Invalid or unsupported device.\n");
            return false;
        }
    } else {
        st = fs_open(path, NULL, FILE_TYPE_REGULAR, 0, &fs_handle);
        if(st) {
            config_error("mezzano: Unable to locate file or device %s: %pS\n",
                         path, st);
            return false;
        }
    }

    *_device = device;
    *_fs_handle = fs_handle;
    return true;
}

/** Load a Mezzano image.
 * @param args        Command arguments.
 * @return        Whether completed successfully. */
static bool config_cmd_mezzano(value_list_t *args) {
    static_assert(offsetof(mezzano_video_information_t, framebuffer_physical_address) == 0);
    static_assert(offsetof(mezzano_video_information_t, framebuffer_width) == 8);
    static_assert(offsetof(mezzano_video_information_t, framebuffer_pitch) == 16);
    static_assert(offsetof(mezzano_video_information_t, framebuffer_height) == 24);
    static_assert(offsetof(mezzano_video_information_t, framebuffer_layout) == 32);
    static_assert(offsetof(mezzano_boot_information_t, uuid) == 0);
    static_assert(offsetof(mezzano_boot_information_t, buddy_bin_32) == 16);
    static_assert(offsetof(mezzano_boot_information_t, buddy_bin_64) == 336);
    static_assert(offsetof(mezzano_boot_information_t, video) == 768);
    static_assert(offsetof(mezzano_boot_information_t, acpi_rsdp) == 808);
    static_assert(offsetof(mezzano_boot_information_t, boot_options) == 816);
    static_assert(offsetof(mezzano_boot_information_t, n_memory_map_entries) == 824);
    static_assert(offsetof(mezzano_boot_information_t, memory_map) == 832);
    static_assert(offsetof(mezzano_boot_information_t, efi_system_table) == 1344);
    static_assert(offsetof(mezzano_boot_information_t, fdt_address) == 1352);
    static_assert(offsetof(mezzano_page_info_t, flags) == 0);
    static_assert(offsetof(mezzano_page_info_t, extra) == 8);
    static_assert(offsetof(mezzano_page_info_t, next) == 16);
    static_assert(offsetof(mezzano_page_info_t, prev) == 24);
    static_assert(offsetof(mezzano_buddy_bin_t, first_page) == 0);
    static_assert(offsetof(mezzano_buddy_bin_t, count) == 8);
    static_assert(offsetof(mezzano_memory_map_entry_t, start) == 0);
    static_assert(offsetof(mezzano_memory_map_entry_t, end) == 8);

    status_t st;
    mezzano_loader_t *data;

    if(args->count < 1 ||
       args->values[0].type != VALUE_TYPE_STRING) {
        config_error("config: mezzano: invalid arguments\n");
        return false;
    }

    device_t *device = NULL;
    fs_handle_t *fs_handle = NULL;
    if(!mezzano_locate_image(args->values[0].string, &device, &fs_handle)) {
        return false;
    }

    data = malloc(sizeof *data);
    memset(data, 0, sizeof *data);
    data->device_name = strdup(args->values[0].string);
    data->disk = device;
    data->fs_handle = fs_handle;

    bool skip_memory_test = false;

    for(unsigned int i = 1; i < args->count; i += 1) {
        if(args->values[i].type != VALUE_TYPE_STRING) {
            config_error("config: mezzano: Bad option");
            goto fail;
        } else if(strcmp(args->values[i].string, "read-only") == 0) {
            data->force_ro = true;
        } else if(strcmp(args->values[i].string, "freestanding") == 0) {
            data->freestanding = true;
        } else if(strcmp(args->values[i].string, "video-console") == 0) {
            data->video_console = true;
        } else if(strcmp(args->values[i].string, "no-detect") == 0) {
            data->no_detect = true;
        } else if(strcmp(args->values[i].string, "no-smp") == 0) {
            data->no_smp = true;
        } else if(strcmp(args->values[i].string, "i-promise-i-have-enough-memory") == 0) {
            skip_memory_test = true;
        } else if(strcmp(args->values[i].string, "verbose") == 0) {
            data->verbose = true;
        } else {
            config_error("config: mezzano: Unsupported option \"%s\"", args->values[i].string);
            goto fail;
        }
    }

    if(!skip_memory_test) {
        // Walk the memory map, summing up regions that look allocatable.
        phys_size_t total_memory = 0;
        list_t mmap;
        memory_snapshot(&mmap);
        list_foreach(&mmap, iter) {
            memory_range_t *range = list_entry(iter, memory_range_t, header);
            total_memory += range->size;
        }

        // Use slightly less than 512MiB as this may not account for all memory.
        if(total_memory < 500ull * 1024 * 1024) {
            config_error("Insufficient memory. Detected %lluMiB, wanted at least 500MiB.\n",
                         total_memory / 1024 / 1024);
            goto fail;
        }
    }

    /* Read in the header. */
    if(data->disk) {
        st = device_read(data->disk, &data->header, sizeof(mezzano_header_t), 0);
    } else {
        st = fs_read(data->fs_handle, &data->header, sizeof(mezzano_header_t), 0);
    }
    if(st) {
        config_error("mezzano: IO error, unable to read header: %pS\n", st);
        goto fail;
    }

    if(memcmp(data->header.magic, mezzano_magic, 16) != 0) {
        config_error("mezzano: Not a mezzano image, bad header.\n");
        goto fail;
    }

    if(data->header.protocol_major != mezzano_protocol_major) {
        config_error("mezzano: Unsupported protocol major %" PRIu8 ".\n",
                 data->header.protocol_major);
        goto fail;
    }

    // Major protocol 0 is for development. The minor must match exactly.
    // Major protocol version above 0 are release version and are backwards
    // compatible at the minor level.
    if((data->header.protocol_major == 0 && data->header.protocol_minor != mezzano_protocol_minor) ||
       (data->header.protocol_major != 0 && data->header.protocol_minor > mezzano_protocol_minor)) {
        config_error("mezzano: Unsupported protocol minor %" PRIu8 ".\n",
                 data->header.protocol_minor);
        goto fail;
    }

#ifdef CONFIG_TARGET_HAS_VIDEO
    if(current_video_mode) {
        video_env_init(current_environ, "video_mode", VIDEO_MODE_LFB, NULL);
    }
#endif

    mprintf("mezzano: Loading image %pU on device %s with protocol version %" PRIu8 ".%" PRIu8 "\n",
            data->header.uuid, data->device_name,
            data->header.protocol_major, data->header.protocol_minor);

    mprintf("mezzano: Entry fref at %08" PRIx64 ". Initial process at %08" PRIx64 ".\n",
            data->header.entry_fref, data->header.initial_process);

    environ_set_loader(current_environ, &mezzano_loader_ops, data);

    return true;
fail:
    if(data->fs_handle) {
        fs_close(data->fs_handle);
    }
    free(data->device_name);
    free(data);
    return false;
}

BUILTIN_COMMAND("mezzano", "Load a Mezzano image", config_cmd_mezzano);
