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
 * @brief               qemu-virt platform main functions.
 */

#include <qemu-virt/qemu-virt.h>

#include <drivers/virtio/virtio.h>

#include <console.h>
#include <device.h>
#include <loader.h>
#include <memory.h>
#include <time.h>
#include <libfdt.h>

static void print_fdt(void) {
    /* look for a flattened device tree at the start of physical memory */
    const void *fdt = (void *)0x40000000;
    int err = fdt_check_header(fdt);
    if (err < 0) {
        dprintf("No device tree detected.\n");
        return;
    }
    dprintf("Device tree:\n");
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

        dprintf("  %s\n", name);

        int prop_offset = fdt_first_property_offset(fdt, offset);
        while(prop_offset >= 0) {
            const struct fdt_property *prop = fdt_get_property_by_offset(fdt, prop_offset, NULL);
            dprintf("    %s\n", fdt_string(fdt, fdt32_to_cpu(prop->nameoff)));
            prop_offset = fdt_next_property_offset(fdt, prop_offset);
        }
    }
}

/** Main function of the qemu-virt platform. */
void qemu_virt_main(void) {
    console_init();

    arch_init();

    print_fdt();
    loader_main();
}

/** Detect and register all devices. */
void target_device_probe(void) {
    virtio_mmio_detect((void *)0x0a000000, 32);
}

/** Reboot the system. */
void target_reboot(void) {
    /* TODO: Call PSCI with SYSTEM_RESET */
    internal_error("Not implemented (reboot)");
}

/** Halt the system. */
__noreturn void target_halt(void) {
    __asm__ volatile("msr daifset, #2" ::: "memory");
    for(;;);
}

/** Get the current internal time.
 * @return              Current internal time. */
mstime_t current_time(void) {
    internal_error("Not implemented");
}
