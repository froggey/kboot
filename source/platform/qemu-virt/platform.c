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
    const void *fdt_address = (void *)0x40000000;
    int err = fdt_check_header(fdt_address);
    if (err < 0) {
        dprintf("No device tree detected.\n");
        return;
    }
    dprintf("Device tree:\n");
    int depth = 0;
    int offset = 0;
    {
        int prop_offset = fdt_first_property_offset(fdt_address, 0);
        while(prop_offset >= 0) {
            const struct fdt_property *prop = fdt_get_property_by_offset(fdt_address, prop_offset, NULL);
            dprintf("  %s\n", fdt_string(fdt_address, fdt32_to_cpu(prop->nameoff)));
            prop_offset = fdt_next_property_offset(fdt_address, prop_offset);
        }
    }
    for (;;) {
        offset = fdt_next_node(fdt_address, offset, &depth);
        if (offset < 0)
            break;

        /* get the name */
        const char *name = fdt_get_name(fdt_address, offset, NULL);
        if (!name)
            continue;

        for(int i = 0; i < depth; i += 1) {
            dprintf("  ");
        }

        dprintf("  %s\n", name);

        int prop_offset = fdt_first_property_offset(fdt_address, offset);
        while(prop_offset >= 0) {
            int prop_len;
            const struct fdt_property *prop = fdt_get_property_by_offset(fdt_address, prop_offset, &prop_len);
            const char *name = fdt_string(fdt_address, fdt32_to_cpu(prop->nameoff));
            for(int i = 0; i < depth; i += 1) {
                dprintf("  ");
            }
            const char *data = prop->data;
            if(strcmp(name, "compatible") == 0 ||
               strcmp(name, "reset-names") == 0 ||
               strcmp(name, "clock-names") == 0) {
                const char *endp = data + prop_len;
                dprintf("    %s = ", name);
                bool first = true;
                while(data < endp) {
                    if(first) {
                        first = false;
                    } else {
                        dprintf(" ");
                    }
                    dprintf("\"%s\"", data);
                    data += strlen(data) + 1;
                }
                dprintf("\n");
            } else if(strcmp(name, "status") == 0 ||
                      strcmp(name, "stdout-path") == 0) {
                dprintf("    %s = %s\n", name, data);
            } else if(strcmp(name, "reg") == 0 ||
                      strcmp(name, "interrupts") == 0 ||
                      strcmp(name, "bus-width") == 0 ||
                      strcmp(name, "resets") == 0 ||
                      strcmp(name, "clocks") == 0 ||
                      strcmp(name, "phandle") == 0 ||
                      strcmp(name, "clock-frequency") == 0 ||
                      strcmp(name, "reg-shift") == 0 ||
                      strcmp(name, "reg-io-width") == 0 ||
                      name[0] == '#') {
                dprintf("    %s = <", name);
                for(int i = 0; i < prop_len / 4; i += 1) {
                    if(i != 0) {
                        dprintf(" ");
                    }
                    dprintf("0x%x", fdt32_to_cpu(((const uint32_t *)data)[i]));
                }
                dprintf(">\n");
            } else {
                dprintf("    %s\n", name);
            }
            prop_offset = fdt_next_property_offset(fdt_address, prop_offset);
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
