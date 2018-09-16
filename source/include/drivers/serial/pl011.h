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

#pragma once

#include <drivers/console/serial.h>

/** UART port definitions. */
#define PL011_DR    (0x00)
#define PL011_RSR   (0x04)
#define PL011_FR    (0x18)
#define PL011_ILPR  (0x20)
#define PL011_IBRD  (0x24)
#define PL011_FBRD  (0x28)
#define PL011_LCRH  (0x2c)
#define PL011_CR    (0x30)
#define PL011_IFLS  (0x34)
#define PL011_IMSC  (0x38)
#define PL011_TRIS  (0x3c)
#define PL011_TMIS  (0x40)
#define PL011_ICR   (0x44)
#define PL011_DMACR (0x48)

typedef ptr_t pl011_base_t;

extern serial_port_t *pl011_register(pl011_base_t base, unsigned index);
