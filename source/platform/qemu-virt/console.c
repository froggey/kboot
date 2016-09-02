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

/* PL011 implementation */
#define UART_DR    (0x00)
#define UART_RSR   (0x04)
#define UART_FR    (0x18)
#define UART_ILPR  (0x20)
#define UART_IBRD  (0x24)
#define UART_FBRD  (0x28)
#define UART_LCRH  (0x2c)
#define UART_CR    (0x30)
#define UART_IFLS  (0x34)
#define UART_IMSC  (0x38)
#define UART_TRIS  (0x3c)
#define UART_TMIS  (0x40)
#define UART_ICR   (0x44)
#define UART_DMACR (0x48)

static volatile uint32_t *const uart_base = (volatile uint32_t *)0x09000000;
static serial_port_t uart;

static status_t uart_port_config(serial_port_t *_port, const serial_config_t *config) {
    (void)_port;
    (void)config;

    // rx_enable,tx_enable, uarten
    uart_base[UART_CR] = (1<<9)|(1<<8)|(1<<0);

    return STATUS_SUCCESS;
}

static bool uart_port_rx_empty(serial_port_t *_port) {
    (void)_port;
    return (uart_base[UART_FR] & (1<<4)) != 0; // RXFE
}

static uint8_t uart_port_read(serial_port_t *_port) {
    (void)_port;
    return uart_base[UART_DR];
}

static bool uart_port_tx_empty(serial_port_t *_port) {
    (void)_port;
    // The TXFE bit seems to be broken on qemu.
    // That's fine, the TXFF bit has mostly the same effect.
    return (uart_base[UART_FR] & (1<<5)) == 0; // TXFF
}

static void uart_port_write(serial_port_t *_port, uint8_t val) {
    uart_base[UART_DR] = val;
}

static const serial_port_ops_t uart_ops = {
    .config = uart_port_config,
    .rx_empty = uart_port_rx_empty,
    .read = uart_port_read,
    .tx_empty = uart_port_tx_empty,
    .write = uart_port_write,
};

/** Initialize the console. */
void target_console_init(void) {
    serial_config_t config;

    config.baud_rate = SERIAL_DEFAULT_BAUD_RATE;
    config.data_bits = SERIAL_DEFAULT_DATA_BITS;
    config.parity = SERIAL_DEFAULT_PARITY;
    config.stop_bits = SERIAL_DEFAULT_STOP_BITS;

    uart.ops = &uart_ops;
    uart.index = 0;

    serial_port_register(&uart);

    serial_port_config(&uart, &config);
    console_set_debug(&uart.console);

    /* TODO: Find a framebuffer. There had better be a framebuffer. */
}
