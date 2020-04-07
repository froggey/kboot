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
 * @brief        Mezzano loader, arm64-specific portions.
 */

#include <loader/mezzano.h>

#include <arm64/mmu.h>

extern void __noreturn mezzano_arch_enter_real(phys_ptr_t transition_ttbr0, phys_ptr_t transition_ttbr1,
                                               phys_ptr_t ttbr0, phys_ptr_t ttbr1,
                                               uint64_t entry_fref, uint64_t initial_process, uint64_t boot_information_location, uint64_t nil,
                                               uint64_t initial_stack_pointer);

void mezzano_arch_enter(mmu_context_t *transition_pml4, mmu_context_t *pml4,
                        uint64_t entry_fref, uint64_t initial_process, uint64_t boot_information_location, uint64_t nil,
                        uint64_t initial_stack_pointer) {
    mezzano_arch_enter_real(transition_pml4->ttbr0, transition_pml4->ttbr1,
                            pml4->ttbr0, pml4->ttbr1,
                            entry_fref, initial_process, boot_information_location, nil,
                            initial_stack_pointer);
}
