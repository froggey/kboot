/*
 * Copyright (C) 2014-2015 Alex Smith
 * Copyright (C) 2016-2018 Henry Harrington
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
 * @brief               PL011 UART driver.
 */

#include <arch/io.h>

#include <drivers/serial/pl011.h>

#include <console.h>
#include <loader.h>
#include <memory.h>

/** PL011 serial port structure. */
typedef struct pl011_port {
    serial_port_t port;                 /**< Serial port header. */

    pl011_base_t base;                  /**< Base of the PL011 registers. */
} pl011_port_t;

static status_t pl011_port_config(serial_port_t *_port, const serial_config_t *config) {
    pl011_port_t *port = (pl011_port_t *)_port;
    (void)config;

    // rx_enable,tx_enable, uarten
    write32((void *)(port->base + PL011_CR),
            (1<<9)|(1<<8)|(1<<0));

    return STATUS_SUCCESS;
}

static bool pl011_port_rx_empty(serial_port_t *_port) {
    pl011_port_t *port = (pl011_port_t *)_port;

    return (read32((void *)(port->base + PL011_FR)) & (1<<4)) != 0; // RXFE
}

static uint8_t pl011_port_read(serial_port_t *_port) {
    pl011_port_t *port = (pl011_port_t *)_port;

    return read32((void *)(port->base + PL011_DR));
}

static bool pl011_port_tx_empty(serial_port_t *_port) {
    pl011_port_t *port = (pl011_port_t *)_port;

    // The TXFE bit seems to be broken on qemu.
    // That's fine, the TXFF bit has mostly the same effect.
    return (read32((void *)(port->base + PL011_FR)) & (1<<5)) == 0; // TXFF
}

static void pl011_port_write(serial_port_t *_port, uint8_t val) {
    pl011_port_t *port = (pl011_port_t *)_port;

    write32((void *)(port->base + PL011_DR), val);
}

static const serial_port_ops_t pl011_port_ops = {
    .config = pl011_port_config,
    .rx_empty = pl011_port_rx_empty,
    .read = pl011_port_read,
    .tx_empty = pl011_port_tx_empty,
    .write = pl011_port_write,
};

/**
 * Register a PL011 UART.
 *
 * Registers a PL011 UART as a console. This function does not reconfigure
 * the UART, to do so use serial_port_config(). If no reconfiguration is done,
 * the UART will continue to use whichever parameters are currently set (e.g.
 * ones set by platform firmware.
 *
 * @param base          Base of UART registers.
 * @param index         Index of the UART, used to name the console.
 *
 * @return              Created port, or NULL if port does not exist.
 */
serial_port_t *pl011_register(pl011_base_t base, unsigned index) {
    pl011_port_t *port = malloc(sizeof(*port));

    port->port.ops = &pl011_port_ops;
    port->port.index = index;
    port->base = base;

    if (serial_port_register(&port->port) != STATUS_SUCCESS) {
        free(port);
        return NULL;
    }

    return &port->port;
}
