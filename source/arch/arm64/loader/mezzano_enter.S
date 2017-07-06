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
 * @param boot_information_location (fixnum) (x6)
 * @param nil                Value of NIL (x7) */
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
     * Should match the MAIR_foo_MEMORY defines in mmu.c
     * Index 0 configured as normal cachable memory.
     * Index 1 configured as normal write-through memory. (??)
     * Index 2 configured as Device-nGnRnE memory.  */
    ldr     x9, =0x00AAFF
    msr     mair_el1, x9

    /* Initialize TCR_EL1 */
    ldr     x9, =((1<<38)| /* TBI1: top byte ignored */\
                  (1<<37)| /* TBI0: top byte ignored */\
                  (5<<32)| /* IPS: 48-bit intermediate physical address. */\
                  (2<<30)| /* TG1: 4k granule (different value to TG0) */\
                  (3<<28)| /* SH1: inner shareable page tables */\
                  (1<<26)| /* ORGN1: write back, write allocate */\
                  (1<<24)| /* IRGN1: write back, write allocate */\
                  (16<<16)| /* T1SZ: 48-bit */\
                  (0<<14)| /* TG0: 4k graunle (different value to TG1) */\
                  (3<<12)| /* SH0: inner shareable page tables */\
                  (1<<10)| /* ORGN1: write back, write allocate */\
                  (1<<8)|  /* IRGN1: write back, write allocate */\
                  (16<<0)) /* T0SZ: 48-bit */
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

    /* Enable the FP and SIMD registers and instructions in EL0 and EL1. */
    mrs     x9, cpacr_el1
    orr     x9, x9, #(3 << 20)
    msr     cpacr_el1, x9
    isb

    /* Load the initial thread into x28 */
    mov     x28, x5

    /* Load NIL into x26 */
    mov     x26, x7

    /* Use SP_EL0. */
    msr     spsel, #0

    /* Load stack from the initial thread */
    ldr     x9, [x28, #31]   /* +thread-stack-pointer+ */
    add     x9, x9, #0xFFF
    and     x9, x9, #~0xFFF
    mov     sp, x9

    /* Prepare for call */
    mov     x0, x6 /* First argument */
    mov     x5, #1<<1 /* One argument */
    mov     x7, x4 /* Call through fref */
    mov     x29, xzr /* End of frame pointer chain */
    mov     x30, xzr /* Return address of zero */

    /* Clear other registers */
    /* Not x0, holds boot info page */
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    mov     x4, xzr
    /* Not x5, holds argument count */
    mov     x6, xzr
    /* Nor x7, holds entry fref */
    mov     x8, xzr
    mov     x9, xzr
    mov     x10, xzr
    mov     x11, xzr
    mov     x12, xzr
    mov     x13, xzr
    mov     x14, xzr
    mov     x15, xzr
    mov     x16, xzr
    mov     x17, xzr
    mov     x18, xzr
    mov     x19, xzr
    mov     x20, xzr
    mov     x21, xzr
    mov     x22, xzr
    mov     x23, xzr
    mov     x24, xzr
    mov     x25, xzr
    /* Not x26, holds nil */
    mov     x27, xzr
    /* Not x28, holds current thread */
    /* Not x29(fp), holds return address */
    /* Not x30(lr), holds frame pointer */

    /* Load the entry point and call the function */
    ldr     x9, [x7, #15] /* +fref-entry-point+ */
    br      x9
FUNCTION_END(mezzano_arch_enter_real)
