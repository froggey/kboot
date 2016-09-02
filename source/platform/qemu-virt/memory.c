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
 * @brief               qemu-virt memory detection code.
 */

#include <qemu-virt/qemu-virt.h>

#include <lib/string.h>
#include <lib/utility.h>

#include <assert.h>
#include <loader.h>
#include <memory.h>

#include <libfdt.h>

uint64_t qemu_virt_total_memory(void) {
    size_t total_memory = 0x08000000; /* 512MB by default */

    /* look for a flattened device tree at the start of physical memory */
    const void *fdt = (void *)0x40000000;
    int err = fdt_check_header(fdt);
    if (err >= 0) {
        /* walk the nodes, looking for 'memory' */
        int depth = 0;
        int offset = 0;
        for (;;) {
            offset = fdt_next_node(fdt, offset, &depth);
            if (offset < 0)
                break;

            /* get the name */
            const char *name = fdt_get_name(fdt, offset, NULL);
            if (!name)
                continue;

            /* look for the properties we care about */
            if (strcmp(name, "memory") == 0) {
                int lenp;
                const void *prop_ptr = fdt_getprop(fdt, offset, "reg", &lenp);
                if (prop_ptr && lenp == 0x10) {
                    /* we're looking at a memory descriptor */
                    //uint64_t base = fdt64_to_cpu(*(uint64_t *)prop_ptr);
                    uint64_t len = fdt64_to_cpu(*((const uint64_t *)prop_ptr + 1));
                    /* set the size in the pmm arena */
                    total_memory = len;
                }
            }
        }
    }

    return total_memory;
}

/** Detect physical memory. */
void target_memory_probe(void) {
    size_t total_memory = qemu_virt_total_memory();

    /* Physical memory starts at 1GB and extends upwards to a maximum of 512GB.
     * Exclude the first 64kb which contains the FDT. */
    memory_add(0x40010000, total_memory-0x10000, MEMORY_TYPE_FREE);

    /* Add the FDT. */
    memory_add(0x40000000, 0x10000, MEMORY_TYPE_RECLAIMABLE);
}
