/*
 * Copyright (C) 2015 Alex Smith
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
 * @brief               arm64 backtrace function.
 */

#include <lib/backtrace.h>

#include <loader.h>

/** Structure containing a stack frame. */
typedef struct stack_frame {
    struct stack_frame *next;       /**< Pointer to next stack frame. */
    ptr_t addr;                     /**< Function return address. */
} stack_frame_t;

/** Print out a backtrace.
 * @param func          Print function to use. */
void backtrace(printf_t func) {
    stack_frame_t *frame;

    func("Backtrace:\n");

    asm volatile("mov %0, x29" : "=r"(frame));
    while (frame && frame->addr) {
        func(" %p\n", frame->addr);
        frame = frame->next;
    }
}
