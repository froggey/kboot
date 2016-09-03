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
 * @param transition_ttbr0   Transition TTBR0. (x0)
 * @param transition_ttbr1   Transition TTBR1. (x1)
 * @param ttbr0              Final TTBR0. (x2)
 * @param ttbr1              Final TTBR1. (x3)
 * @param entry_fref         Value of the entry fref. (x4)
 * @param initial_process    Value of the initial process. (x5)
 * @param boot_information_location (fixnum) (x6) */
FUNCTION_START(mezzano_arch_enter_real)
    /* Caches on */
    mrs     x9, sctlr_el1
    orr     x9, x9, #(1<<12) /* Enable icache */
    orr     x9, x9, #(1<<2)  /* Enable dcache/ucache */
    msr     sctlr_el1, x9

    /* Invalidate TLB */
    tlbi    vmalle1
    isb
    dsb     sy

    /* Initialize Memory Attribute Indirection Register
     * Index 0 configured as normal cachable memory */
    ldr     x9, =0xFF
    msr     mair_el1, x9

    /* Initialize TCR_EL1
     * TODO: Set the IPS field properly, currently defaulting to 4GB. */
    /* TG1, 4k granule (different to TG0!) */
    /* SH1, inner shareable */
    /* ORGN1, write-back + write-allocate */
    /* IRGN1, write-back + write-allocate */
    /* T1SZ, 48-bit */
    /* TG0, 4k granule (different to TG1!) */
    /* SH0, inner shareable */
    /* ORGN0, write-back + write-allocate */
    /* IRGN0, write-back + write-allocate */
    /* T0SZ, 48-bit */
    ldr     x9, =((2<<30)|(3<<28)|(1<<26)|(1<<24)|(16<<16)|(0<<14)|(3<<12)|(1<<10)|(1<<8)|(16<<0))
    msr     tcr_el1, x9
    isb

    /* Write ttbr with phys addr of the transition translation tables */
    msr     ttbr0_el1, x0
    msr     ttbr1_el1, x1
    isb

    /* Turn on the MMU */
    mrs     x9, sctlr_el1
    orr     x9, x9, #0x1
    msr     sctlr_el1, x9
    isb

    /* Move to the pmap. */
    ldr     x9, =0xFFFF800000000000
    adr     x10, 1f
    add     x9, x10, x9
    br      x9
1:

    /* Switch to the real translation table. */
    msr     ttbr0_el1, x2
    msr     ttbr1_el1, x3
    isb

    /* Invalidate TLB */
    tlbi    vmalle1
    isb


    b .
FUNCTION_END(mezzano_arch_enter_real)
