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
 * @brief               orange-pi-pc2 memory detection code.
 */

#include <orange-pi-pc2/orange-pi-pc2.h>

#include <lib/string.h>
#include <lib/utility.h>

#include <assert.h>
#include <loader.h>
#include <memory.h>

#include <libfdt.h>

uint64_t orange_pi_pc2_total_memory(void) {
    return 1ul * 1024 * 1024 * 1024; /* 1GB */
}

static uint32_t read_prop_u32(const void *fdt, int offset, const char *prop_name, uint32_t dflt) {
    int lenp;
    const void *prop_ptr = fdt_getprop(fdt, offset, prop_name, &lenp);
    if(prop_ptr == NULL || lenp < 4) {
        return dflt;
    }
    return fdt32_to_cpu(*((const uint32_t *)prop_ptr));
}

static uint64_t read_cell_value(const void **ptr, uint32_t n_cells) {
    uint64_t value = 0;
    for(uint32_t i = 0; i < n_cells; i += 1) {
        value = value << 32;
        value |= fdt32_to_cpu(*((const uint32_t *)*ptr));
        *ptr += 4;
    }
    return value;
}

/** Detect physical memory. */
void target_memory_probe(void) {
    phys_ptr_t initrd_start = 0;
    phys_ptr_t initrd_end = 0;

    /* Get the #address-cells and #size-cells fields. */
    uint32_t n_root_addr_cells = read_prop_u32(fdt_address, 0, "#address-cells", 1);
    uint32_t n_root_size_cells = read_prop_u32(fdt_address, 0, "#size-cells", 1);

    dprintf("/#address-cells: %u\n", n_root_addr_cells);
    dprintf("/#size-cells: %u\n", n_root_size_cells);

    /* Find the /memory node */
    int memory_offset = fdt_path_offset(fdt_address, "/memory");
    if(memory_offset < 0) {
        internal_error("Missing /memory FDT node");
    }

    /* And the size/address properties */
    uint32_t n_addr_cells = read_prop_u32(fdt_address, memory_offset, "#address-cells", n_root_addr_cells);
    uint32_t n_size_cells = read_prop_u32(fdt_address, memory_offset, "#size-cells", n_root_size_cells);

    {
        /* Reg prop contains address/len pairs. */
        int lenp;
        const void *prop_ptr = fdt_getprop(fdt_address, memory_offset, "reg", &lenp);
        if(prop_ptr == NULL) {
            internal_error("Missing /memory/reg FDT property");
        }
        uint32_t n_cells = lenp / 4;
        uint32_t n_entries = n_cells / (n_addr_cells + n_size_cells);
        dprintf("memory reg prop at %p len %i  %u cells, %u entries\n", prop_ptr, lenp, n_cells, n_entries);

        for(uint32_t i = 0; i < n_entries; i += 1) {
            uint64_t address = read_cell_value(&prop_ptr, n_addr_cells);
            uint64_t size = read_cell_value(&prop_ptr, n_size_cells);
            dprintf("Add memory range %llx - %llx\n", address, address + size);
            memory_add(address, size, MEMORY_TYPE_FREE);
        }
    }

    /* Look for the initrd too. */
    int chosen_offset = fdt_path_offset(fdt_address, "/chosen");
    if(chosen_offset >= 0) {
        {
            int len;
            const void *prop_ptr = fdt_getprop(fdt_address, chosen_offset, "linux,initrd-start", &len);
            if(prop_ptr) {
                if(len == 4) {
                    initrd_start = fdt32_to_cpu(*(const uint32_t *)prop_ptr);
                } else if(len == 8) {
                    initrd_start = fdt64_to_cpu(*(const uint64_t *)prop_ptr);
                }
            }
        }

        {
            int len;
            const void *prop_ptr = fdt_getprop(fdt_address, chosen_offset, "linux,initrd-end", &len);
            if(prop_ptr) {
                if(len == 4) {
                    initrd_end = fdt32_to_cpu(*(const uint32_t *)prop_ptr);
                } else if(len == 8) {
                    initrd_end = fdt64_to_cpu(*(const uint64_t *)prop_ptr);
                }
            }
        }
    }

    /* Protect the FDT. */
    memory_reserve((phys_ptr_t)fdt_address, 0x10000, MEMORY_TYPE_RECLAIMABLE);

    /* The initrd doesn't get passed on to the kernel. */
    if(initrd_start != 0 && initrd_end != 0) {
        initrd_address = initrd_start;
        initrd_size = initrd_end - initrd_start;
        memory_reserve((phys_ptr_t)initrd_start, initrd_size, MEMORY_TYPE_INTERNAL);
    }
}
