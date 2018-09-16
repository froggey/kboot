/*
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
 * @brief               generic-arm64 startup code.
 */

#include <arch/page.h>
#include <arm64/asm.h>
#include <arm64/mmu.h>
#include <platform/loader.h>

.section .text.startup, "ax", @progbits

linux_header:
    b       _start                          // branch to kernel start. code0
    .long   0                               // code1
	.quad   LOADER_PHYS_OFFSET              // Image load offset from start of RAM
	.quad   0                               // effective image size
	.quad   0                               // reserved
	.quad   0                               // reserved
	.quad   0                               // reserved
	.quad   0                               // reserved
	.byte   0x41                            // Magic number, "ARM\x64"
	.byte   0x52
	.byte   0x4d
	.byte   0x64
	.word   0                               // reserved

FUNCTION_START(_start)
    /* Clear frame pointer. */
    mov x29, #0

    /* Clear BSS */
	adrp x2, __bss_end__
	adrp x1, __bss_start
	add x1, x1, :lo12:__bss_start
	add x2, x2, :lo12:__bss_end__
	subs x2, x2, x1
	mov x3, 0
	beq 2f
	.p2align 2
1:
	strb wzr, [x3, x1]
	add x3, x3, 1
	cmp x3, x2
	bne 1b
2:
    /* Drop to EL1 if booted in EL2
     * TODO: Do something with EL3 too? */
    mrs x1, CurrentEl
    cmp x1, #(1 << 2)
    beq .el1

    /* Return to 64-bit EL1 */
    mov x1, #0x80000000 /* RW=64-bit, no other traps or virtualization. */
    msr hcr_el2, x1
    adr x1, .el1
    msr elr_el2, x1
    mov x1, #0x000003C5 /* DAIF set, returning to AArch64 EL1h (sp is sp_el1) */
    msr spsr_el2, x1
    isb
    eret
.el1:
    /* Load stack pointer. */
    ldr x9, =__stack_end
    mov sp, x9

    /* Load exception vector. */
    adr x1, exception_base
    msr vbar_el1, x1
    isb

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
     * Must match the ARM64_MAIR_foo_MEMORY defines in arm64/mmu.h
     * Index 0 configured as normal cachable memory.
     * Index 1 configured as normal write-through memory. (??)
     * Index 2 configured as Device-nGnRnE memory.  */
    ldr     x9, =0x00AAFF
    msr     mair_el1, x9

    /* Initialize TCR_EL1 */
    ldr     x9, =((0<<37)| /* TBI0: top byte not ignored */\
                  (5<<32)| /* IPS: 48-bit intermediate physical address. */\
                  (1<<23)| /* EPD1: TTBR1 disabled */\
                  (0<<14)| /* TG0: 4k graunle (different value to TG1) */\
                  (3<<12)| /* SH0: inner shareable page tables */\
                  (1<<10)| /* ORGN1: write back, write allocate */\
                  (1<<8)|  /* IRGN1: write back, write allocate */\
                  (16<<0)) /* T0SZ: 48-bit */
    msr     tcr_el1, x9
    isb

    /* Write ttbr with phys addr of the transition translation tables */
    ldr     x9, =identity_ttl0
    msr     ttbr0_el1, x9
    isb

    /* Turn on the MMU */
    mrs     x9, sctlr_el1
    orr     x9, x9, #0x1
    msr     sctlr_el1, x9
    isb

    /* Call main */
    bl generic_arm64_main
    b .
FUNCTION_END(_start)

.ltorg

.section .text, "ax", @progbits

.p2align 12
exception_base:
.org 0x000
synchronous_el1t_handler: // sync exception from EL1 using SP_EL0
    ldr x1, =0x01C28000
    ldr x2, ='s'
1:  str x2, [x1]
    b 1b
.org 0x080
irq_el1t_handler: // irq from EL1 using SP_EL0
    b .
.org 0x100
fiq_el1t_handler: // fiq from EL1 using SP_EL0
    b .
.org 0x180
serror_el1t_handler: // serror from EL1 using SP_EL0
    b .
.org 0x200
synchronous_el1h_handler: // sync exception from EL1 using SP_ELx
    stp x0, x1, [sp,#-16]!
    stp x2, x3, [sp,#-16]!
    stp x4, x5, [sp,#-16]!
    stp x6, x7, [sp,#-16]!
    stp x8, x9, [sp,#-16]!
    stp x10, x11, [sp,#-16]!
    stp x12, x13, [sp,#-16]!
    stp x14, x15, [sp,#-16]!
    stp x16, x17, [sp,#-16]!
    stp x18, x19, [sp,#-16]!
    stp x20, x21, [sp,#-16]!
    stp x22, x23, [sp,#-16]!
    stp x24, x25, [sp,#-16]!
    stp x26, x27, [sp,#-16]!
    stp x28, x29, [sp,#-16]!
    add x0, sp, #(30 * 8)
    stp x30, x0, [sp,#-16]!
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp,#-16]!
    mov x0, sp
    bl arm64_unhandled_sync_exception
.org 0x280
irq_el1h_handler: // irq from EL1 using SP_ELx
    b .
.org 0x300
fiq_el1h_handler: // fiq from EL1 using SP_ELx
    b .
.org 0x380
serror_el1h_handler: // serror from EL1 using SP_ELx
    b .
.org 0x400
synchronous_el0_64_handler:
    b .
.org 0x480
irq_el0_64_handler:
    b .
.org 0x500
fiq_el0_64_handler:
    b .
.org 0x580
serror_el0_64_handler:
    b .
.org 0x600
synchronous_el0_32_handler:
    b .
.org 0x680
irq_el0_32_handler:
    b .
.org 0x700
fiq_el0_32_handler:
    b .
.org 0x780
serror_el0_32_handler:
    b .

.section .data, "aw", @progbits

.p2align 12
identity_ttl0:
    .quad identity_ttl1 + (ARM64_TTE_PRESENT | ARM64_TTE_TABLE)
    .fill 511, 8, 0

identity_ttl1:
    .quad (identity_ttl2 + 4096 * 0) + (ARM64_TTE_PRESENT | ARM64_TTE_TABLE)
    .quad (identity_ttl2 + 4096 * 1) + (ARM64_TTE_PRESENT | ARM64_TTE_TABLE)
    .quad (identity_ttl2 + 4096 * 2) + (ARM64_TTE_PRESENT | ARM64_TTE_TABLE)
    .quad (identity_ttl2 + 4096 * 3) + (ARM64_TTE_PRESENT | ARM64_TTE_TABLE)
    .fill 508, 8, 0

identity_ttl2:
    /* Identity map 0-1G as uncached memory using 2M pages. */
    i = 0
    .rept 512
        .quad i | ARM64_TTE_PRESENT | ARM64_TTE_AF | ARM64_TTE_AP_P_RW_U_NA | ARM64_TTE_ATTR_INDEX(ARM64_MAIR_DEVICE_MEMORY)
        i = i + 0x200000
    .endr
    /* Identity map 1G-2G as normal memory using 2M pages. */
    .rept 512
        .quad i | ARM64_TTE_PRESENT | ARM64_TTE_AF | ARM64_TTE_SH_INNER_SHAREABLE | ARM64_TTE_AP_P_RW_U_NA | ARM64_TTE_ATTR_INDEX(ARM64_MAIR_NORMAL_MEMORY)
        i = i + 0x200000
    .endr
    /* Identity map 0-1G as uncached memory using 2M pages. */
    .rept 512
        .quad i | ARM64_TTE_PRESENT | ARM64_TTE_AF | ARM64_TTE_AP_P_RW_U_NA | ARM64_TTE_ATTR_INDEX(ARM64_MAIR_DEVICE_MEMORY)
        i = i + 0x200000
    .endr
    /* Identity map 0-1G as uncached memory using 2M pages. */
    .rept 512
        .quad i | ARM64_TTE_PRESENT | ARM64_TTE_AF | ARM64_TTE_AP_P_RW_U_NA | ARM64_TTE_ATTR_INDEX(ARM64_MAIR_DEVICE_MEMORY)
        i = i + 0x200000
    .endr

.section .bss, "aw", @nobits

.balign 16
/** Stack. */
.type loader_stack, @object
loader_stack:
    .fill   PAGE_SIZE
__stack_end:
