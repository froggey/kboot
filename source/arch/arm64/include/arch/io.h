/*
 * Copyright (C) 2014 Alex Smith
 * Copyright (C) 2017 Henry Harrington
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
 * @brief               arm64 I/O functions.
 */

#ifndef __ARCH_IO_H
#define __ARCH_IO_H

#include <types.h>

/**
 * Memory mapped I/O functions.
 */

/** Read an 8 bit value from a memory mapped register.
 * @param addr          Address to read from.
 * @return              Value read. */
static inline uint8_t read8(const volatile uint8_t *addr) {
    return *addr;
}

/** Write an 8 bit value to a memory mapped register.
 * @param addr          Address to write to.
 * @param val           Value to write. */
static inline void write8(volatile uint8_t *addr, uint8_t val) {
    *addr = val;
}

/** Read a 16 bit value from a memory mapped register.
 * @param addr          Address to read from.
 * @return              Value read. */
static inline uint16_t read16(const volatile uint16_t *addr) {
    return *addr;
}

/** Write a 16 bit value to a memory mapped register.
 * @param addr          Address to write to.
 * @param val           Value to write. */
static inline void write16(volatile uint16_t *addr, uint16_t val) {
    *addr = val;
}

/** Read a 32 bit value from a memory mapped register.
 * @param addr          Address to read from.
 * @return              Value read. */
static inline uint32_t read32(const volatile uint32_t *addr) {
    return *addr;
}

/** Write a 32 bit value to a memory mapped register.
 * @param addr          Address to write to.
 * @param val           Value to write. */
static inline void write32(volatile uint32_t *addr, uint32_t val) {
    *addr = val;
}

#endif /* __ARCH_IO_H */
