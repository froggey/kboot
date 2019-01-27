/*
 * Copyright (C) 2015 Alex Smith
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
 * @brief               generic-arm64 platform KBoot loader functions.
 */

#include <lib/string.h>

#include <loader/kboot.h>

#include <generic-arm64/generic-arm64.h>

#include <assert.h>
#include <memory.h>

/** Perform platform-specific setup for a KBoot kernel.
 * @param loader        Loader internal data. */
void kboot_platform_setup(kboot_loader_t *loader) {
    kboot_tag_fdt_t *tag;

    /* Pass the FDT to the kernel. */
    tag = kboot_alloc_tag(loader, KBOOT_TAG_FDT, sizeof(*tag));
    tag->fdt = (kboot_paddr_t)fdt_address;
}
