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

#include <drivers/console/serial.h>
#include <drivers/serial/ns16550.h>

#define UART0_BASE 0x01C28000

/** Initialize the console. */
void target_console_init(void) {
    serial_config_t config;

    config.baud_rate = SERIAL_DEFAULT_BAUD_RATE;
    config.data_bits = SERIAL_DEFAULT_DATA_BITS;
    config.parity = SERIAL_DEFAULT_PARITY;
    config.stop_bits = SERIAL_DEFAULT_STOP_BITS;

    serial_port_t *port = ns16550_register(UART0_BASE, 0, 115200);

    serial_port_config(port, &config);
    console_set_debug(&port->console);

    /* TODO: Find a framebuffer. There had better be a framebuffer. */
}
