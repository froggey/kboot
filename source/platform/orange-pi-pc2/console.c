/*
 * Copyright (C) 2014-2015 Alex Smith
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
 * @brief               qemu-virt console functions.
 */

#include <assert.h>
#include <console.h>
#include <loader.h>
#include <memory.h>
#include <libfdt.h>

#include <drivers/console/serial.h>
#include <drivers/serial/ns16550.h>
#include <drivers/serial/pl011.h>

#include <orange-pi-pc2/orange-pi-pc2.h>

#include <platform/fdt.h>

/** Initialize the console. */
void target_console_init(void) {
    serial_config_t config;

    config.baud_rate = SERIAL_DEFAULT_BAUD_RATE;
    config.data_bits = SERIAL_DEFAULT_DATA_BITS;
    config.parity = SERIAL_DEFAULT_PARITY;
    config.stop_bits = SERIAL_DEFAULT_STOP_BITS;

    int chosen = fdt_path_offset(fdt_address, "/chosen");
    if(chosen < 0) {
        return;
    }
    int len;
    const char *stdout = fdt_getprop(fdt_address, chosen, "stdout-path", &len);
    if(!stdout || !len) {
        return;
    }

    const char *options_marker = strchr(stdout, ':');
    if(options_marker) {
        len = options_marker - stdout;
    }

    int con_dev = fdt_path_offset_namelen(fdt_address, stdout, len);
    if(con_dev < 0) {
        return;
    }

    serial_port_t *port = NULL;

    if(!fdt_node_check_compatible(fdt_address, con_dev, "snps,dw-apb-uart")) {
        phys_ptr_t base, size;
        platform_fdt_get_reg(con_dev, 0, &base, &size);
        port = ns16550_register(base, 0, 115200); // TODO: Figure out what this should be
    } else if(!fdt_node_check_compatible(fdt_address, con_dev, "arm,pl011")) {
        phys_ptr_t base, size;
        platform_fdt_get_reg(con_dev, 0, &base, &size);
        port = pl011_register(base, 0);
    }

    serial_port_config(port, &config);
    console_set_debug(&port->console);
}
