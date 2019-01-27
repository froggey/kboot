/*
 * Copyright (C) 2010-2015 Alex Smith
 * Copyright (C) 2019 Henry Harrington
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
 * @brief               arm64 KBoot kernel entry functions.
 */

#include <arch/page.h>

#include <platform/loader.h>

#include <arm64/asm.h>

#include <kboot.h>

.section .text, "ax", @progbits

/** Offsets in entry_args_t. */
#define ENTRY_ARGS_TRAMPOLINE_TTBR0 0
#define ENTRY_ARGS_TRAMPOLINE_TTBR1 8
#define ENTRY_ARGS_TRAMPOLINE_VIRT  16
#define ENTRY_ARGS_KERNEL_TTBR0     24
#define ENTRY_ARGS_KERNEL_TTBR1     32
#define ENTRY_ARGS_SP               40
#define ENTRY_ARGS_ENTRY            48
#define ENTRY_ARGS_TAGS             56
#define ENTRY_ARGS_TRAMPOLINE       64

/** 64-bit kernel trampoline. */
FUNCTION_START(kboot_trampoline_64)
    /* Switch to the real kernel page directory. */
    ldr     x9, [x0, #ENTRY_ARGS_KERNEL_TTBR0]
    msr     ttbr0_el1, x9
    ldr     x9, [x0, #ENTRY_ARGS_KERNEL_TTBR1]
    msr     ttbr1_el1, x9
    isb

    /* Invalidate TLB */
    tlbi    vmalle1
    isb
    dsb     sy

    /* Set the stack pointer. */
    ldr     x9, [x0, #ENTRY_ARGS_SP]
    mov     sp, x9

    /* Clear the stack frame. */
    mov     x29, #0

    /* Retrieve tag list address and entry point. */
    ldr     x1, [x0, #ENTRY_ARGS_TAGS]
    ldr     x9, [x0, #ENTRY_ARGS_ENTRY]

    /* Call the kernel. */
    ldr     x0, =KBOOT_MAGIC
    blr     x9
1:  b       1b
.pool

SYMBOL(kboot_trampoline_64_size)
    .long   . - kboot_trampoline_64
FUNCTION_END(kboot_trampoline_64)

/** Enter a 64-bit KBoot kernel.
 * @param args          Entry arguments structure. */
FUNCTION_START(kboot_arch_enter_64)
    /* Get the address of the trampoline PML4. */
    ldr     x9, [x0, #ENTRY_ARGS_TRAMPOLINE_TTBR0]
    ldr     x10, [x0, #ENTRY_ARGS_TRAMPOLINE_TTBR1]

    /* Save the correct virtual address for the trampoline (64-bit). */
    ldr     x0, [x0, #ENTRY_ARGS_TRAMPOLINE_VIRT]

    /* Initialize TCR_EL1 */
    ldr     x11, =((1<<38)| /* TBI1: top byte ignored */\
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
    msr     tcr_el1, x11
    isb

    /* Now switch to trampoline PML4. */
    msr     ttbr0_el1, x9
    msr     ttbr1_el1, x10
    isb

    /* Jump to the trampoline. */
    add     x9, x0, #ENTRY_ARGS_TRAMPOLINE
    br      x9
FUNCTION_END(kboot_arch_enter_64)
