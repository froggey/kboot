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
 * @brief               arm64 MMU definitions.
 */

#ifndef __ARM64_MMU_H
#define __ARM64_MMU_H

#include <loader.h>

/** Definitions of paging structure bits. */
#define ARM64_TTE_PRESENT         (1<<0)  /**< Page is present. */
#define ARM64_TTE_TABLE           (1<<1)  /**< Page is a table. */
#define ARM64_TTE_PAGE            (1<<1)  /**< Page is a page. */

#define ARM64_TTE_AF              (1<<10) /**< Page has been accessed. */
#define ARM64_TTE_AP_P_RW_U_NA    (0<<6)  /**< Protected RW, user not accessible. */
#define ARM64_TTE_AP_P_RW_U_RW    (1<<6)  /**< Protected RW, user RW. */
#define ARM64_TTE_AP_P_RO_U_NA    (2<<6)  /**< Protected RO, user not accessible. */
#define ARM64_TTE_AP_P_RO_U_RO    (3<<6)  /**< Protected RO, user RO. */

#define ARM64_TTE_SH_NON_SHAREABLE   (0<<8)
#define ARM64_TTE_SH_OUTER_SHAREABLE (2<<8)
#define ARM64_TTE_SH_INNER_SHAREABLE (3<<8)

/** Masks to get physical address from a page table entry. */
#define ARM64_TTE_ADDR_MASK       0x00007ffffffff000ull

/** Ranges covered by paging structures. */
#define ARM64_TTL1_RANGE          0x8000000000ull
#define ARM64_TTL2_RANGE          0x40000000
#define ARM64_TTL3_RANGE          0x200000

/** arm64 MMU context structure. */
struct mmu_context {
    phys_ptr_t ttbr0;                   /**< Value loaded into ttbr0_el1. */
    phys_ptr_t ttbr1;                   /**< Value loaded into ttbr1_el1. */
    unsigned phys_type;                 /**< Physical memory type for page tables. */
};

/** Check whether an address is canonical.
 * @param addr          Address to check.
 * @return              Result of check. */
static inline bool is_canonical_addr(uint64_t addr) {
    return ((uint64_t)((int64_t)addr >> 48) + 1) <= 1;
}

/** Check whether an address range is canonical.
 * @param start         Start of range to check.
 * @param size          Size of address range.
 * @return              Result of check. */
static inline bool is_canonical_range(uint64_t start, uint64_t size) {
    uint64_t end = start + size - 1;

    return is_canonical_addr(start)
        && is_canonical_addr(end)
        && (start & (1ull << 48)) == (end & (1ull << 48));
}

#endif /* __ARM64_MMU_H */
