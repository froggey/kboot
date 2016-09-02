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
 * @brief               x86 Mezzano entry functions.
 */

#include <arch/page.h>

#include <platform/loader.h>

#include <x86/asm.h>
#include <x86/cpu.h>
#include <x86/descriptor.h>

#include <kboot.h>

.section .text, "ax", @progbits

/** Enter a Mezzano image.
 * @param transition_pml4    Transition PML4.
 * @param pml4            Final PML4.
 * @param entry_fref        Value of the entry fref.
 * @param initial_process    Value of the initial process.
 * @param boot_information_location (fixnum) */
FUNCTION_START(mezzano_arch_enter_real)
.code32
    /* Enable PAE, PGE, and SSE. */
    movl    %cr4, %eax
    orl    $X86_CR4_PAE|X86_CR4_PGE|X86_CR4_OSFXSR|X86_CR4_OSXMMEXCPT, %eax
    movl    %eax, %cr4

    /* Point CR3 to the transition PML4. */
    movl    4(%esp), %eax
    movl    %eax, %cr3

    /* Enable long mode by setting EFER.LME. */
    movl    $X86_MSR_EFER, %ecx
    rdmsr
    orl    $X86_EFER_LME, %eax
    wrmsr

    /* Configure CR0. This will also put us in compatibility mode. */
    movl    $X86_CR0_PE|X86_CR0_MP|X86_CR0_ET|X86_CR0_NE|X86_CR0_WP|X86_CR0_PG, %ecx
    movl    %ecx, %cr0

    /* Jump into the 64-bit code segment. */
    ljmp    $SEGMENT_CS64, $1f
.align 8
.code64
1:
    /* Set data segments. */
    xorl    %eax, %eax
    mov    %ax, %ds
    mov    %ax, %es
    mov    %ax, %fs
    mov    %ax, %gs
    mov    %ax, %ss

    /* Fetch the arguments from the stack. */
    movq    20(%esp), %r13   /* entry fref */
    movq    28(%esp), %r12   /* initial process */
    movq    36(%esp), %r8    /* boot info location */

    /* Move to the pmap. */
    movq    $0xFFFF800000000000, %rax
    leaq    1f(%rip), %rcx
    addq    %rcx, %rax
    jmp     *%rax
1:

    /* Switch to the real kernel page directory. */
    movq    12(%esp), %rax
    movq    %rax, %cr3

    /* Load the initial process into GS. */
    movq    %r12, %rax
    movq    %r12, %rdx
    sarq    $32, %rdx
    movl    $X86_MSR_GS_BASE, %ecx
    wrmsr

    /* Load stack from the initial process */
    movq    31(%r12), %rsp   /* +thread-stack-pointer+ */
    addq    $0xFFF, %rsp
    andq    $~0xFFF, %rsp

    /* Clear FPU/SSE state. */
    push    $0x1F80     /* All exceptions masked. Flush to zero disabled. Denormals are zero disabled. Round to nearest. */
    ldmxcsr (%rsp)
    addq    $8, %rsp
    fninit

    /* Clear flags. */
    push    $0
    popf

    /* Clear registers. */
    xorl    %eax, %eax
    xorl    %edx, %edx
    xorl    %ebx, %ebx
    xorl    %ebp, %ebp
    xorl    %esi, %esi
    xorl    %edi, %edi
    xorl    %r9d, %r9d
    xorl    %r10d, %r10d
    xorl    %r11d, %r11d
    xorl    %r12d, %r12d
    /* Not r13, holds the entry fref */
    xorl    %r14d, %r14d
    xorl    %r15d, %r15d

    /* Call the entry function. Single argument call. */
    push    $0 /* Return address of zero. */
    movl    $1<<1, %ecx
    jmp     *15(%r13)  /* +fref-entry-point+ */
FUNCTION_END(mezzano_arch_enter_real)
