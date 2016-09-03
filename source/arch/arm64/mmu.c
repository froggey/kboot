/*
 * Copyright (C) 2011-2015 Alex Smith
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
 * @brief               arm64 MMU functions.
 */

#include <arch/page.h>

#include <arm64/mmu.h>

#include <lib/string.h>
#include <lib/utility.h>

#include <assert.h>
#include <loader.h>
#include <memory.h>
#include <mmu.h>

/** Allocate a paging structure.
 * @param ctx           Context to allocate for.
 * @return              Physical address allocated. */
static phys_ptr_t allocate_structure(mmu_context_t *ctx) {
    phys_ptr_t phys;
    void *virt;

    /* Allocate high to try to avoid any fixed kernel load location. */
    virt = memory_alloc(PAGE_SIZE, PAGE_SIZE, 0, 0, ctx->phys_type, MEMORY_ALLOC_HIGH, &phys);
    memset(virt, 0, PAGE_SIZE);
    return phys;
}

/** Get a page directory from a 64-bit context.
 * @param ctx           Context to get from.
 * @param virt          Virtual address to get for (can be non-aligned).
 * @param alloc         Whether to allocate if not found.
 * @return              Address of page directory, or NULL if not found. */
static uint64_t *get_ttl2(mmu_context_t *ctx, uint64_t virt, bool alloc) {
    uint64_t *ttl0, *ttl1;
    phys_ptr_t addr;
    unsigned ttl0e, ttl1e;

    if(virt & 0x8000000000000000ull) {
        ttl0 = (uint64_t *)phys_to_virt(ctx->ttbr1);
    } else {
        ttl0 = (uint64_t *)phys_to_virt(ctx->ttbr0);
    }

    /* Get the page directory pointer number. */
    ttl0e = (virt / ARM64_TTL1_RANGE) % 512;
    if (!(ttl0[ttl0e] & ARM64_TTE_PRESENT)) {
        if (!alloc)
            return NULL;

        addr = allocate_structure(ctx);
        ttl0[ttl0e] = addr | ARM64_TTE_PRESENT | ARM64_TTE_TABLE;
    }

    /* Get the TTL1 from the TTL0. */
    ttl1 = (uint64_t *)phys_to_virt((ptr_t)(ttl0[ttl0e] & ARM64_TTE_ADDR_MASK));

    /* Get the page directory number. */
    ttl1e = (virt % ARM64_TTL1_RANGE) / ARM64_TTL2_RANGE;
    if (!(ttl1[ttl1e] & ARM64_TTE_PRESENT)) {
        if (!alloc)
            return NULL;

        addr = allocate_structure(ctx);
        ttl1[ttl1e] = addr | ARM64_TTE_PRESENT | ARM64_TTE_TABLE;
    }

    /* Return the page directory address. */
    return (uint64_t *)phys_to_virt((ptr_t)(ttl1[ttl1e] & ARM64_TTE_ADDR_MASK));
}

/** Map a large page in a 64-bit context.
 * @param ctx           Context to map in.
 * @param virt          Virtual address to map.
 * @param phys          Physical address to map to. */
static void map_large(mmu_context_t *ctx, uint64_t virt, uint64_t phys) {
    uint64_t *ttl2;
    unsigned pde;

    assert(!(virt % LARGE_PAGE_SIZE));
    assert(!(phys % LARGE_PAGE_SIZE));

    ttl2 = get_ttl2(ctx, virt, true);
    pde = (virt % ARM64_TTL2_RANGE) / LARGE_PAGE_SIZE;
    ttl2[pde] = phys | ARM64_TTE_PRESENT | ARM64_TTE_AF | ARM64_TTE_SH_INNER_SHAREABLE | ARM64_TTE_AP_P_RW_U_NA; // TODO: Cache attributes.
}

/** Map a small page in a 64-bit context.
 * @param ctx           Context to map in.
 * @param virt          Virtual address to map.
 * @param phys          Physical address to map to. */
static void map_small(mmu_context_t *ctx, uint64_t virt, uint64_t phys) {
    uint64_t *ttl2, *ttl3;
    phys_ptr_t addr;
    unsigned pde, pte;

    assert(!(virt % PAGE_SIZE));
    assert(!(phys % PAGE_SIZE));

    ttl2 = get_ttl2(ctx, virt, true);

    /* Get the page directory entry number. */
    pde = (virt % ARM64_TTL2_RANGE) / ARM64_TTL3_RANGE;
    if (!(ttl2[pde] & ARM64_TTE_PRESENT)) {
        addr = allocate_structure(ctx);
        ttl2[pde] = addr | ARM64_TTE_PRESENT | ARM64_TTE_TABLE;
    }

    /* Get the page table from the page directory. */
    ttl3 = (uint64_t *)phys_to_virt((ptr_t)(ttl2[pde] & ARM64_TTE_ADDR_MASK));

    /* Map the page. */
    pte = (virt % ARM64_TTL3_RANGE) / PAGE_SIZE;
    ttl3[pte] = phys | ARM64_TTE_PRESENT | ARM64_TTE_PAGE | ARM64_TTE_AF | ARM64_TTE_SH_INNER_SHAREABLE | ARM64_TTE_AP_P_RW_U_NA; // TODO: Cache attributes.
}

/** Create a mapping in an MMU context.
 * @param ctx           Context to map in.
 * @param virt          Virtual address to map.
 * @param phys          Physical address to map to.
 * @param size          Size of the mapping to create.
 * @return              Whether the supplied addresses were valid. */
bool mmu_map(mmu_context_t *ctx, load_ptr_t virt, phys_ptr_t phys, load_size_t size) {
    assert(!(virt % PAGE_SIZE));
    assert(!(phys % PAGE_SIZE));
    assert(!(size % PAGE_SIZE));

    if (!is_canonical_range(virt, size))
        return false;

    /* Map using large pages where possible (always supported on 64-bit). To do
     * this, align up to a 2MB boundary using small pages, map anything possible
     * with large pages, then do the rest using small pages. If virtual and
     * physical addresses are at different offsets from a large page boundary,
     * we cannot map using large pages. */
    if ((virt % LARGE_PAGE_SIZE) == (phys % LARGE_PAGE_SIZE)) {
        while (virt % LARGE_PAGE_SIZE && size) {
            map_small(ctx, virt, phys);
            virt += PAGE_SIZE;
            phys += PAGE_SIZE;
            size -= PAGE_SIZE;
        }
        while (size / LARGE_PAGE_SIZE) {
            map_large(ctx, virt, phys);
            virt += LARGE_PAGE_SIZE;
            phys += LARGE_PAGE_SIZE;
            size -= LARGE_PAGE_SIZE;
        }
    }

    /* Map whatever remains. */
    while (size) {
        map_small(ctx, virt, phys);
        virt += PAGE_SIZE;
        phys += PAGE_SIZE;
        size -= PAGE_SIZE;
    }

    return true;
}

/** Memory operation mode. */
enum {
    MMU_MEM_SET,
    MMU_MEM_COPY_TO,
    MMU_MEM_COPY_FROM,
};

/** Perform a memory operation. */
static void do_mem_op(phys_ptr_t page, size_t page_size, unsigned op, ptr_t *_value) {
    void *ptr = (void *)phys_to_virt(page);

    if (op == MMU_MEM_SET) {
        memset(ptr, (uint8_t)*_value, page_size);
    } else {
        if (op == MMU_MEM_COPY_TO) {
            memcpy(ptr, (const void *)*_value, page_size);
        } else {
            memcpy((void *)*_value, ptr, page_size);
        }

        *_value += page_size;
    }
}

/** Memory operation on 64-bit MMU context.
 * @param ctx           Context to operate on.
 * @param addr          Virtual address to operate on.
 * @param size          Size of range.
 * @param op            Operation to perform.
 * @param value         Value to set, or loader source/destination address.
 * @return              Whether the address range was valid. */
static bool mmu_mem_op(mmu_context_t *ctx, uint64_t addr, uint64_t size, unsigned op, ptr_t value) {
    uint64_t *ttl2 = NULL, *ttl3 = NULL;

    if (!is_canonical_range(addr, size))
        return false;

    while (size) {
        phys_ptr_t page = 0;
        size_t page_size = 0;

        /* If we have crossed a page directory boundary, get new directory. */
        if (!ttl2 || !(addr % ARM64_TTL2_RANGE)) {
            ttl2 = get_ttl2(ctx, addr, false);
            if (!ttl2)
                return false;
        }

        /* Same for page table. */
        if (!ttl3 || !(addr % ARM64_TTL3_RANGE)) {
            unsigned pde = (addr % ARM64_TTL2_RANGE) / ARM64_TTL3_RANGE;
            if (!(ttl2[pde] & ARM64_TTE_PRESENT))
                return false;

            if (ttl2[pde] & ARM64_TTE_TABLE) {
                ttl3 = (uint64_t *)phys_to_virt((ptr_t)(ttl2[pde] & ARM64_TTE_ADDR_MASK));
            } else {
                page = (ttl2[pde] & ARM64_TTE_ADDR_MASK) + (addr % LARGE_PAGE_SIZE);
                page_size = LARGE_PAGE_SIZE - (addr % LARGE_PAGE_SIZE);
                ttl3 = NULL;
            }
        }

        if (ttl3) {
            unsigned pte = (addr % ARM64_TTL3_RANGE) / PAGE_SIZE;
            if (!(ttl3[pte] & ARM64_TTE_PRESENT))
                return false;

            page = (ttl3[pte] & ARM64_TTE_ADDR_MASK) + (addr % PAGE_SIZE);
            page_size = PAGE_SIZE - (addr % PAGE_SIZE);
        }

        page_size = min(page_size, size);

        do_mem_op(page, page_size, op, &value);

        addr += page_size;
        size -= page_size;
    }

    return true;
}

/** Set bytes in an area of virtual memory.
 * @param ctx           Context to use.
 * @param addr          Virtual address to write to, must be mapped.
 * @param value         Value to write.
 * @param size          Number of bytes to write.
 * @return              Whether the range specified was valid. */
bool mmu_memset(mmu_context_t *ctx, load_ptr_t addr, uint8_t value, load_size_t size) {
    return mmu_mem_op(ctx, addr, size, MMU_MEM_SET, value);
}

/** Copy to an area of virtual memory.
 * @param ctx           Context to use.
 * @param dest          Virtual address to write to, must be mapped.
 * @param src           Memory to read from.
 * @param size          Number of bytes to copy.
 * @return              Whether the range specified was valid. */
bool mmu_memcpy_to(mmu_context_t *ctx, load_ptr_t dest, const void *src, load_size_t size) {
    return mmu_mem_op(ctx, dest, size, MMU_MEM_COPY_TO, (ptr_t)src);
}

/** Copy from an area of virtual memory.
 * @param ctx           Context to use.
 * @param dest          Memory to write to.
 * @param src           Virtual address to read from, must be mapped.
 * @param size          Number of bytes to copy.
 * @return              Whether the range specified was valid. */
bool mmu_memcpy_from(mmu_context_t *ctx, void *dest, load_ptr_t src, load_size_t size) {
    return mmu_mem_op(ctx, src, size, MMU_MEM_COPY_FROM, (ptr_t)dest);
}

/** Create a new MMU context.
 * @param mode          Load mode for the OS.
 * @param phys_type     Physical memory type to use when allocating tables.
 * @return              Pointer to context. */
mmu_context_t *mmu_context_create(load_mode_t mode, unsigned phys_type) {
    mmu_context_t *ctx;

    assert(mode == LOAD_MODE_64BIT);

    ctx = malloc(sizeof(*ctx));
    ctx->phys_type = phys_type;
    ctx->ttbr0 = allocate_structure(ctx);
    ctx->ttbr1 = allocate_structure(ctx);
    return ctx;
}
