/*
 * Copyright (C) 2014, 2015 Henry Harrington
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
 * @brief		Mezzano loader.
 *
 * This file implements the 'mezzano' command for loading a Mezzano image.
 *
 * The 'mezzano' command is used as follows:
 *
 *   mezzano "<device name>"
 */

#include <lib/string.h>
#include <lib/utility.h>

#include <bios/bios.h>
#include <bios/memory.h>

#include <x86/mmu.h>

#include <assert.h>
#include <device.h>
#include <disk.h>
#include <loader.h>
#include <memory.h>
#include <mmu.h>
#include <ui.h>
#include <video.h>

/** A virtual memory extent that should be loaded by the bootloader. */
typedef struct mezzanine_extent {
	uint64_t virtual_base;
	uint64_t size;
	uint64_t flags;
	uint64_t extra;
} __packed mezzanine_extent_t;

/** On-disk image header. */
typedef struct mezzanine_header {
	uint8_t magic[16];		/* +0 */
	uint8_t uuid[16];		/* +16 */
	uint16_t protocol_major;	/* +32 */
	uint16_t protocol_minor;	/* +34 */
	uint32_t n_extents;		/* +36 */
	uint64_t entry_fref;		/* +40 */
	uint64_t initial_process;	/* +48 */
	uint64_t nil;			/* +56 */
	uint8_t _pad2[32];		/* +64 */
	uint64_t bml4;   		/* +96 */
	uint64_t freelist_head;		/* +104 */
	mezzanine_extent_t extents[64];	/* +112 */
} __packed mezzanine_header_t;

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

typedef struct mezzanine_page_info {
	uint64_t flags;
	uint64_t extra;
	uint64_t next;
	uint64_t prev;
} __packed mezzanine_page_info_t;

typedef struct mezzanine_buddy_bin {
	uint64_t first_page;
	uint64_t count;
} __packed mezzanine_buddy_bin_t;

typedef struct mezzanine_memory_map_entry {
	uint64_t start;
	uint64_t end;
} __packed mezzanine_memory_map_entry_t;

static const char mezzanine_magic[] = "\x00MezzanineImage\x00";
static const uint16_t mezzanine_protocol_major = 0;
static const uint16_t mezzanine_protocol_minor = 22;
// FIXME: Duplicated in enter.S
static const uint64_t mezzanine_physical_map_address = 0xFFFF800000000000ull;
static const uint64_t mezzanine_physical_info_address = 0xFFFF808000000000ull;
static const uint64_t mezzanine_physical_map_size = 0x8000000000ull;
#define log2_4k_page 12
// Modifying these will change the layout of the boot info page.
// Changes the protocol version to be bumped.
// 32 bits - log2(4k_page_size)
#define mezzanine_n_buddy_bins_32_bit (32-log2_4k_page)
// The physical map covers 512GB, log2 is 39.
#define mezzanine_n_buddy_bins_64_bit (39-log2_4k_page)
#define mezzanine_max_memory_map_size 32

/* Boot info page */

typedef struct mezzanine_video_information {
	// Framebuffer size is stride * height, aligned up to page size.
	uint64_t framebuffer_physical_address;                     //  +0 fixnum.
	uint64_t framebuffer_width;                                //  +8 fixnum, pixels.
	uint64_t framebuffer_pitch;                                // +16 fixnum, bytes.
	uint64_t framebuffer_height;                               // +24 fixnum, pixels.
	uint64_t framebuffer_layout;                               // +32 fixnum.
} __packed mezzanine_video_information_t;

// Common(ish) layouts. Layouts beyond 32-bit XRGB will be supported in later boot protocols.
#define FRAMEBUFFER_LAYOUT_X8_R8_G8_B8 1  // 32-bit XRGB
//#define FRAMEBUFFER_LAYOUT_R8_G8_B8_X8 2  // 32-bit RGBX
//#define FRAMEBUFFER_LAYOUT_X8_B8_G8_R8 3  // 32-bit XBGR
//#define FRAMEBUFFER_LAYOUT_B8_G8_R8_X8 4  // 32-bit BGRX
//#define FRAMEBUFFER_LAYOUT_X0_R8_G8_B8 5  // 24-bit RGB
//#define FRAMEBUFFER_LAYOUT_X0_B8_G8_R8 6  // 24-bit BGR
//#define FRAMEBUFFER_LAYOUT_X0_R5_G6_B5 7  // 16-bit 565 RGB
//#define FRAMEBUFFER_LAYOUT_X0_B5_G6_R5 8  // 16-bit 565 BGR
//#define FRAMEBUFFER_LAYOUT_X1_R5_G5_B5 9  // 16-bit 555 XRGB
//#define FRAMEBUFFER_LAYOUT_X1_B5_G5_R5 10 // 16-bit 555 XBGR
//#define FRAMEBUFFER_LAYOUT_R5_G5_B5_X1 11 // 16-bit 555 RGBX
//#define FRAMEBUFFER_LAYOUT_B5_G5_R5_X1 12 // 16-bit 555 BGRX

typedef struct mezzanine_boot_information {
	uint8_t uuid[16];                                          // +0 octets.
	// Buddy allocator for memory below 4GB.
	mezzanine_buddy_bin_t buddy_bin_32[mezzanine_n_buddy_bins_32_bit]; // +16
	// Buddy allocator for remaining memory.
	mezzanine_buddy_bin_t buddy_bin_64[mezzanine_n_buddy_bins_64_bit]; // +336
	// Video information.
	mezzanine_video_information_t video;                       // +768
	uint64_t acpi_rsdp;                                        // +808
	uint8_t _pad[8];                                           // +816
	// The memory map specifies where RAM exists, not what it can be used for.
	// If it's in the memory map, then it has an info struct mapped.
	// This is sorted in address order, with no overlaps.
	uint64_t n_memory_map_entries;                             // +824 unsigned-byte 64.
	mezzanine_memory_map_entry_t memory_map[mezzanine_max_memory_map_size];
} __packed mezzanine_boot_information_t;

#define BLOCK_MAP_PRESENT 1
#define BLOCK_MAP_WRITABLE 2
#define BLOCK_MAP_ZERO_FILL 4
#define BLOCK_MAP_FLAG_MASK 0xFF
#define BLOCK_MAP_ID_SHIFT 8

typedef struct block_cache_entry {
	uint64_t block;
	void *data;
	struct block_cache_entry *next;
} block_cache_entry_t;

/** Structure containing Mezzanine image loader state. */
typedef struct mezzanine_loader {
	device_t *disk;			/**< Image device. */
	char *device_name;		/**< Image device name. */

	mezzanine_header_t header;

	block_cache_entry_t *block_cache;
} mezzanine_loader_t;

extern void __noreturn mezzanine_arch_enter(phys_ptr_t transition_pml4, phys_ptr_t pml4, uint64_t entry_fref, uint64_t initial_process, uint64_t boot_information_location);

static bool in_range(uint64_t start, uint64_t end, uint64_t value) {
	return start <= value && value <= end;
}

static void crunch_memory_map(mezzanine_boot_information_t *boot_info) {
	// Merge all overlapping/contiguous regions in the memory map.
	// insert_into_memory_map may produce opportunities for merging.

	// TODO.
}

static void insert_into_memory_map(mezzanine_boot_information_t *boot_info, uint64_t start, uint64_t end) {
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
	if(boot_info->n_memory_map_entries == mezzanine_max_memory_map_size) {
		dprintf("Too many memory map entries. Ignoring %016" PRIx64 "-%016" PRIx64 "\n",
			start, end);
		return;
	}
	// Shuffle entries up.
	memmove(&boot_info->memory_map[i+1], &boot_info->memory_map[i], (boot_info->n_memory_map_entries - i) * sizeof(mezzanine_memory_map_entry_t));
	// Set.
	boot_info->memory_map[i].start = start;
	boot_info->memory_map[i].end = end;
	boot_info->n_memory_map_entries += 1;

	crunch_memory_map(boot_info);
}


static void generate_memory_map(mmu_context_t *mmu, mezzanine_boot_information_t *boot_info) {
	// Iterate the E820 memory map to generate the physical mapping region.
	// Only memory mentioned by the E820 map is mapped here. Device memory, etc is
	// left for the OS to detect and map.
	// TODO, eventually the system will need to know about ACPI reclaim/NVS areas.

	void *buf __cleanup_free;
	size_t num_entries, entry_size, size;

	bios_memory_get_mmap(&buf, &num_entries, &entry_size);
	size = num_entries * entry_size;

	for(size_t i = 0; i < num_entries; i++) {
		e820_entry_t *entry = buf + (i * entry_size);

		// Map liberally, it doesn't matter if free regions overlap with allocated
		// regions. It's more important that the entire region is mapped.
		phys_ptr_t start = round_down(entry[i].start, PAGE_SIZE);
		phys_ptr_t end = round_up(entry[i].start + entry[i].length, PAGE_SIZE);

		// Ignore this region if exceeds the map area limit.
		if(start >= mezzanine_physical_map_size) {
			continue;
		}

		// Trim it if it starts below the limit.
		if(end > mezzanine_physical_map_size) {
			end = mezzanine_physical_map_size;
		}

		/* What we did above may have made the region too small,
		 * ignore it if this is the case. */
		if(end <= start) {
			continue;
		}

		dprintf("mezzanine: Map E820 region %016" PRIx64 "-%016" PRIx64 " %016" PRIx64 "-%016" PRIx64 "\n",
			entry[i].start, entry[i].start + entry[i].length, start, end);

		// Map the memory into the physical map region.
		mmu_map(mmu, mezzanine_physical_map_address + start, start, end - start);

		// Carefully insert it into the memory map, maintaining the sortedness.
		insert_into_memory_map(boot_info, start, end);
	}

	dprintf("mezzanine: Final memory map:\n");
	for(uint64_t i = 0; i < boot_info->n_memory_map_entries; i += 1) {
		dprintf("  %016" PRIx64 "-%016" PRIx64 "\n",
			boot_info->memory_map[i].start,
			boot_info->memory_map[i].end);
	}

	// Allocate the information structs for all the pages in the memory map.
	// FIXME: This has a bit of a problem with overlap, leaking a few pages.
	for(uint64_t i = 0; i < boot_info->n_memory_map_entries; i += 1) {
		phys_ptr_t start = boot_info->memory_map[i].start;
		phys_ptr_t end = boot_info->memory_map[i].end;
		phys_ptr_t info_start = round_down((mezzanine_physical_info_address + (start / PAGE_SIZE) * sizeof(mezzanine_page_info_t)), PAGE_SIZE);
		phys_ptr_t info_end = round_up((mezzanine_physical_info_address + (end / PAGE_SIZE) * sizeof(mezzanine_page_info_t)), PAGE_SIZE);
		phys_ptr_t phys_info_addr;
		dprintf("mezzanine: info range %016" PRIx64 "-%016" PRIx64 "\n", info_start, info_end);
		// FIXME/TODO: It's ok for the backing pages to be discontinuous.
		// Could use 2MB pages here as well.
		void *virt = memory_alloc(info_end - info_start, // size
					  0x1000, // alignment
					  0x100000, 0, // min/max address
					  MEMORY_TYPE_ALLOCATED, // type
					  0, // flags
					  &phys_info_addr);
		mmu_map(mmu, info_start, phys_info_addr, info_end - info_start);
		memset(virt, 0, info_end - info_start);
	}
}

/// Convert val to a fixnum.
static uint64_t fixnum(int64_t val) {
	return val << 1;
}
// And the other way.
static int64_t unfixnum(uint64_t fix) {
	return ((int64_t)fix) >> 1;
}

static uint64_t page_info_flags(mmu_context_t *mmu, phys_ptr_t page) {
	uint64_t offset = page / PAGE_SIZE;
	uint64_t result;
	mmu_memcpy_from(mmu, &result, mezzanine_physical_info_address + offset * sizeof(mezzanine_page_info_t) + offsetof(mezzanine_page_info_t, flags), sizeof result);
	return result;
}

static void set_page_info_flags(mmu_context_t *mmu, phys_ptr_t page, uint64_t value) {
	uint64_t offset = page / PAGE_SIZE;
	mmu_memcpy_to(mmu, mezzanine_physical_info_address + offset * sizeof(mezzanine_page_info_t) + offsetof(mezzanine_page_info_t, flags), &value, sizeof value);
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
	mmu_memcpy_from(mmu, &result, mezzanine_physical_info_address + offset * sizeof(mezzanine_page_info_t) + offsetof(mezzanine_page_info_t, extra), sizeof result);
	return result;
}
*/

static void set_page_info_extra(mmu_context_t *mmu, phys_ptr_t page, uint64_t value) {
	uint64_t offset = page / PAGE_SIZE;
	mmu_memcpy_to(mmu, mezzanine_physical_info_address + offset * sizeof(mezzanine_page_info_t) + offsetof(mezzanine_page_info_t, extra), &value, sizeof value);
}

static uint64_t page_info_next(mmu_context_t *mmu, phys_ptr_t page) {
	uint64_t offset = page / PAGE_SIZE;
	uint64_t result;
	mmu_memcpy_from(mmu, &result, mezzanine_physical_info_address + offset * sizeof(mezzanine_page_info_t) + offsetof(mezzanine_page_info_t, next), sizeof result);
	return result;
}

static void set_page_info_next(mmu_context_t *mmu, phys_ptr_t page, uint64_t value) {
	uint64_t offset = page / PAGE_SIZE;
	mmu_memcpy_to(mmu, mezzanine_physical_info_address + offset * sizeof(mezzanine_page_info_t) + offsetof(mezzanine_page_info_t, next), &value, sizeof value);
}

static uint64_t page_info_prev(mmu_context_t *mmu, phys_ptr_t page) {
	uint64_t offset = page / PAGE_SIZE;
	uint64_t result;
	mmu_memcpy_from(mmu, &result, mezzanine_physical_info_address + offset * sizeof(mezzanine_page_info_t) + offsetof(mezzanine_page_info_t, prev), sizeof result);
	return result;
}

static void set_page_info_prev(mmu_context_t *mmu, phys_ptr_t page, uint64_t value) {
	uint64_t offset = page / PAGE_SIZE;
	mmu_memcpy_to(mmu, mezzanine_physical_info_address + offset * sizeof(mezzanine_page_info_t) + offsetof(mezzanine_page_info_t, prev), &value, sizeof value);
}

static phys_ptr_t buddy(int k, phys_ptr_t x) {
	return x ^ ((phys_ptr_t)1 << (k + log2_4k_page));
}

static bool page_exists(mezzanine_boot_information_t *boot_info, phys_ptr_t page) {
	for(uint64_t i = 0; i < boot_info->n_memory_map_entries; i += 1) {
		if(boot_info->memory_map[i].start <= page && page < boot_info->memory_map[i].end) {
			return true;
		}
	}
	return false;
}

static void buddy_free_page(mmu_context_t *mmu, mezzanine_boot_information_t *boot_info, uint64_t nil, phys_ptr_t l) {
	//phys_ptr_t orig = l;
	mezzanine_buddy_bin_t *buddies;
	int m;
	int k = 0;

	if(l < 0x100000000ull) { // 4GB
		m = mezzanine_n_buddy_bins_32_bit - 1;
		buddies = boot_info->buddy_bin_32;
	} else {
		m = mezzanine_n_buddy_bins_64_bit - 1;
		buddies = boot_info->buddy_bin_64;
	}

	//if(orig > 0x000000013FF00000ull)
	//	dprintf("Freeing page %016" PRIx64 "\n", l);

	while(1) {
		phys_ptr_t p = buddy(k, l);
		//if(orig > 0x000000013FF00000ull)
		//	dprintf(" buddy %016" PRIx64 "  order %i\n", p, k);

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
	//	dprintf(" final %016" PRIx64 "\n", l);

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

static void set_video_mode(mezzanine_boot_information_t *boot_info)
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

	dprintf("mezzanine: Using %ix%i video mode, layout %i, pitch %i, fb at %08x\n",
		mode->width, mode->height, layout, mode->pitch, mode->mem_phys);
	boot_info->video.framebuffer_physical_address = fixnum(mode->mem_phys);
	boot_info->video.framebuffer_width = fixnum(mode->width);
	boot_info->video.framebuffer_pitch = fixnum(mode->pitch);
	boot_info->video.framebuffer_height = fixnum(mode->height);
	boot_info->video.framebuffer_layout = fixnum(layout);
}

static void *read_cached_block(mezzanine_loader_t *loader, uint64_t block_id) {
	block_cache_entry_t **prev = &loader->block_cache;
	for(block_cache_entry_t *e = loader->block_cache; e; e = e->next) {
		if(e->block == block_id) {
			// Bring recently used blocks to the front of the list.
			*prev = e->next;
			e->next = loader->block_cache;
			loader->block_cache = e;
			return e->data;
		}
		prev = &e->next;
	}
	// Not present in cache. Read from disk.
	block_cache_entry_t *e = malloc(sizeof(block_cache_entry_t));
	e->next = loader->block_cache;
	loader->block_cache = e;
	e->block = block_id;

	// Heap is fixed-size, use pages directly.
	phys_ptr_t phys_addr;
	e->data = memory_alloc(0x1000, // size
			       0x1000, // alignment
			       0, 0, // min/max address
			       MEMORY_TYPE_INTERNAL, // type
			       0, // flags
			       &phys_addr);

	if(!device_read(loader->disk,
			e->data,
			0x1000,
			block_id * 0x1000)) {
		boot_error("Could not read block %" PRIu64, block_id);
	}

	return e->data;
}

static uint64_t read_info_for_page(mezzanine_loader_t *loader, uint64_t virtual) {
	// Indices into each level of the block map.
	uint64_t bml4i = (virtual >> 39) & 0x1FF;
	uint64_t bml3i = (virtual >> 30) & 0x1FF;
	uint64_t bml2i = (virtual >> 21) & 0x1FF;
	uint64_t bml1i = (virtual >> 12) & 0x1FF;
	uint64_t *bml4 = read_cached_block(loader, loader->header.bml4);
	if((bml4[bml4i] & BLOCK_MAP_PRESENT) == 0) {
		return 0;
	}
	uint64_t *bml3 = read_cached_block(loader, bml4[bml4i] >> BLOCK_MAP_ID_SHIFT);
	if((bml3[bml3i] & BLOCK_MAP_PRESENT) == 0) {
		return 0;
	}
	uint64_t *bml2 = read_cached_block(loader, bml3[bml3i] >> BLOCK_MAP_ID_SHIFT);
	if((bml2[bml2i] & BLOCK_MAP_PRESENT) == 0) {
		return 0;
	}
	uint64_t *bml1 = read_cached_block(loader, bml2[bml2i] >> BLOCK_MAP_ID_SHIFT);
	return bml1[bml1i];
}

static void load_page(mezzanine_loader_t *loader, mmu_context_t *mmu, uint64_t virtual) {
	uint64_t info = read_info_for_page(loader, virtual);
	if((info & BLOCK_MAP_PRESENT) == 0) {
		return;
	}

	// Alloc phys.
	phys_ptr_t phys_addr;
	void *bootloader_virt = memory_alloc(PAGE_SIZE, // size
					     0x1000, // alignment
					     0x100000, 0, // min/max address
					     MEMORY_TYPE_ALLOCATED, // type
					     0, // flags
					     &phys_addr);
	// Map...
	mmu_map(mmu, virtual, phys_addr, PAGE_SIZE);
	// Write block number to page info struct.
	set_page_info_extra(mmu, phys_addr, fixnum(info >> BLOCK_MAP_ID_SHIFT));
	set_page_info_type(mmu, phys_addr, page_type_wired);

	if(info & BLOCK_MAP_ZERO_FILL) {
		memset(bootloader_virt, 0, PAGE_SIZE);
	} else {
		if(!device_read(loader->disk,
				bootloader_virt,
				0x1000,
				(info >> BLOCK_MAP_ID_SHIFT) * 0x1000)) {
			boot_error("Could not read block %" PRIu64 " for virtual address %" PRIx64, info, virtual);
		}
	}
}

static void dump_one_buddy_allocator(mmu_context_t *mmu, mezzanine_boot_information_t *boot_info, uint64_t nil, mezzanine_buddy_bin_t *buddies, int max) {
	for(int k = 0; k < max; k += 1) {
		dprintf("Order %i %" PRIu64 " %016" PRIx64 ":\n", (k + log2_4k_page), buddies[k].count, buddies[k].first_page);
		uint64_t current = buddies[k].first_page;
		while(true) {
			if(current == nil) {
				break;
			}
			assert((current & 1) == 0);
			dprintf("  %016" PRIx64 "-%016" PRIx64 " %016" PRIx64 " %016" PRIx64 "\n",
				unfixnum(current) * PAGE_SIZE,
				unfixnum(current) * PAGE_SIZE + ((phys_ptr_t)1 << (12+k)),
				page_info_next(mmu, unfixnum(current) * PAGE_SIZE),
				page_info_prev(mmu, unfixnum(current) * PAGE_SIZE));
			current = page_info_next(mmu, unfixnum(current) * PAGE_SIZE);
		}
	}
}

static void dump_buddy_allocator(mmu_context_t *mmu, mezzanine_boot_information_t *boot_info, uint64_t nil) {
	dprintf("32-bit buddy allocator:\n");
	dump_one_buddy_allocator(mmu, boot_info, nil, boot_info->buddy_bin_32, mezzanine_n_buddy_bins_32_bit);
	dprintf("64-bit buddy allocator:\n");
	dump_one_buddy_allocator(mmu, boot_info, nil, boot_info->buddy_bin_64, mezzanine_n_buddy_bins_64_bit);
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

/** Load the operating system. */
static __noreturn void mezzanine_loader_load(void *_loader) {
	mezzanine_loader_t *loader = _loader;
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
	mezzanine_boot_information_t *boot_info = boot_info_page_virt;
	memset(boot_info, 0, PAGE_SIZE);

	generate_memory_map(mmu, boot_info);

	for(uint32_t i = 0; i < loader->header.n_extents; ++i) {
		// Each extent must be 4k (page) aligned in memory.
		dprintf("mezzanine: extent % 2" PRIu32 " %016" PRIx64 " %08" PRIx64 " %04" PRIx64 "\n",
			i, loader->header.extents[i].virtual_base,
			loader->header.extents[i].size, loader->header.extents[i].flags);
		if(loader->header.extents[i].virtual_base % PAGE_SIZE ||
		   loader->header.extents[i].size % PAGE_SIZE) {
			boot_error("Extent %" PRIu32 " is misaligned", i);
		}

		// Load each page in the region.
		// TODO: Use 2M pages.
		for(uint64_t offset = 0; offset < loader->header.extents[i].size; offset += PAGE_SIZE) {
			load_page(loader, mmu, loader->header.extents[i].virtual_base + offset);
		}
	}

	boot_info->acpi_rsdp = acpi_detect();

	memcpy(boot_info->uuid, loader->header.uuid, 16);

	loader_preboot();

	set_video_mode(boot_info);

	// Initialize buddy bins.
	for(int i = 0; i < mezzanine_n_buddy_bins_32_bit; ++i) {
		boot_info->buddy_bin_32[i].first_page = loader->header.nil;
		boot_info->buddy_bin_32[i].count = fixnum(0);
	}
	for(int i = 0; i < mezzanine_n_buddy_bins_64_bit; ++i) {
		boot_info->buddy_bin_64[i].first_page = loader->header.nil;
		boot_info->buddy_bin_64[i].count = fixnum(0);
	}

	// Generate the page tables used for transitioning from identity mapping
	// to the final page tables.
	// The loader must be identity mapped, and mapped in the physical region.
	ptr_t loader_start = round_down((ptr_t)__start, PAGE_SIZE);
	ptr_t loader_size = round_up((ptr_t)__end - (ptr_t)__start, PAGE_SIZE);
	mmu_map(transition, loader_start, loader_start, loader_size);
	mmu_map(transition, mezzanine_physical_map_address + loader_start, loader_start, loader_size);
	mmu_map(mmu, mezzanine_physical_map_address + loader_start, loader_start, loader_size);

	/* Reclaim all memory used internally. */
	list_t kboot_memory_map;
	memory_finalize(&kboot_memory_map);

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

	dump_buddy_allocator(mmu, boot_info, loader->header.nil);

	dprintf("mezzanine: Starting system...\n");
	mezzanine_arch_enter(transition->cr3,
			     mmu->cr3,
			     loader->header.entry_fref,
			     loader->header.initial_process,
			     fixnum(mezzanine_physical_map_address + boot_info_page));
}

#if CONFIG_KBOOT_UI

/** Return a window for configuring the OS.
 * @return		Pointer to configuration window. */
static ui_window_t *mezzanine_loader_configure(void) {
	return NULL;
}

#endif

/** Mezzanine loader type. */
static loader_ops_t mezzanine_loader_ops = {
	.load = mezzanine_loader_load,
#if CONFIG_KBOOT_UI
	.configure = mezzanine_loader_configure,
#endif
};

/** Load a Mezzanine image.
 * @param args		Command arguments.
 * @return		Whether completed successfully. */
static bool config_cmd_mezzanine(value_list_t *args) {
	static_assert(offsetof(mezzanine_video_information_t, framebuffer_physical_address) == 0);
	static_assert(offsetof(mezzanine_video_information_t, framebuffer_width) == 8);
	static_assert(offsetof(mezzanine_video_information_t, framebuffer_pitch) == 16);
	static_assert(offsetof(mezzanine_video_information_t, framebuffer_height) == 24);
	static_assert(offsetof(mezzanine_video_information_t, framebuffer_layout) == 32);
	static_assert(offsetof(mezzanine_boot_information_t, uuid) == 0);
	static_assert(offsetof(mezzanine_boot_information_t, buddy_bin_32) == 16);
	static_assert(offsetof(mezzanine_boot_information_t, buddy_bin_64) == 336);
	static_assert(offsetof(mezzanine_boot_information_t, video) == 768);
	static_assert(offsetof(mezzanine_boot_information_t, acpi_rsdp) == 808);
	static_assert(offsetof(mezzanine_boot_information_t, n_memory_map_entries) == 824);
	static_assert(offsetof(mezzanine_boot_information_t, memory_map) == 832);
	static_assert(offsetof(mezzanine_page_info_t, flags) == 0);
	static_assert(offsetof(mezzanine_page_info_t, extra) == 8);
	static_assert(offsetof(mezzanine_page_info_t, next) == 16);
	static_assert(offsetof(mezzanine_page_info_t, prev) == 24);
	static_assert(offsetof(mezzanine_buddy_bin_t, first_page) == 0);
	static_assert(offsetof(mezzanine_buddy_bin_t, count) == 8);
	static_assert(offsetof(mezzanine_memory_map_entry_t, start) == 0);
	static_assert(offsetof(mezzanine_memory_map_entry_t, end) == 8);

	mezzanine_loader_t *data;

	if(args->count != 1 ||
	   args->values[0].type != VALUE_TYPE_STRING) {
		dprintf("config: mezzanine: invalid arguments\n");
		return false;
	}

	device_t *device = device_lookup(args->values[0].string);

	/* Verify the device exists and is compatible (disks only, currently). */
	if(!device || device->type != DEVICE_TYPE_DISK) {
		dprintf("mezzanine: Invalid or unsupported device.\n");
		return false;
	}

	data = malloc(sizeof *data);
	data->block_cache = NULL;
	data->device_name = strdup(args->values[0].string);
	data->disk = device;

	/* Read in the header. */
	if(!device_read(data->disk, &data->header, sizeof(mezzanine_header_t), 0)) {
		dprintf("mezzanine: IO error, unable to read header.\n");
		goto fail;
	}

	if(memcmp(data->header.magic, mezzanine_magic, 16) != 0) {
		dprintf("mezzanine: Not a mezzanine image, bad header.\n");
		goto fail;
	}

	if(data->header.protocol_major != mezzanine_protocol_major) {
		dprintf("mezzanine: Unsupported protocol major %" PRIu8 ".\n",
			data->header.protocol_major);
		goto fail;
	}

	// Major protocol 0 is for development. The minor must match exactly.
	// Major protocol version above 0 are release version and are backwards
	// compatible at the minor level.
	if((data->header.protocol_major == 0 && data->header.protocol_minor != mezzanine_protocol_minor) ||
	   (data->header.protocol_major != 0 && data->header.protocol_minor > mezzanine_protocol_minor)) {
		dprintf("mezzanine: Unsupported protocol minor %" PRIu8 ".\n",
			data->header.protocol_minor);
		goto fail;
	}

	dprintf("mezzanine: Loading image %02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x on device %s with protocol version %" PRIu8 ".%" PRIu8 "\n",
		data->header.uuid[0], data->header.uuid[1], data->header.uuid[2],
		data->header.uuid[3], data->header.uuid[4], data->header.uuid[5],
		data->header.uuid[6], data->header.uuid[7], data->header.uuid[8],
		data->header.uuid[9], data->header.uuid[10], data->header.uuid[11],
		data->header.uuid[12], data->header.uuid[13], data->header.uuid[14],
		data->header.uuid[15],
		data->device_name,
		data->header.protocol_major, data->header.protocol_minor);

	dprintf("mezzanine: Entry fref at %08" PRIx64 ". Initial process at %08" PRIx64 ".\n",
		data->header.entry_fref, data->header.initial_process);

	current_environ->loader = &mezzanine_loader_ops;

	return true;
fail:
	free(data->device_name);
	free(data);
	return false;
}

BUILTIN_COMMAND("mezzano", "Load a Mezzano image", config_cmd_mezzanine);
