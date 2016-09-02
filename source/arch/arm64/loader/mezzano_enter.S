/*
 * Copyright (C) 2014-2016 Henry Harrington
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
 * @brief               arm64 Mezzano entry functions.
 */

#include <arch/page.h>

#include <arm64/asm.h>

#include <platform/loader.h>

#include <kboot.h>

.section .text, "ax", @progbits

/** Enter a Mezzano image.
 * @param transition_ttbr0   Transition TTBR0.
 * @param transition_ttbr1   Transition TTBR1.
 * @param ttbr0              Final TTBR0.
 * @param ttbr1              Final TTBR1.
 * @param entry_fref         Value of the entry fref.
 * @param initial_process    Value of the initial process.
 * @param boot_information_location (fixnum) */
FUNCTION_START(mezzano_arch_enter_real)
        b .
FUNCTION_END(mezzano_arch_enter_real)
