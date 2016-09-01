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
 * @brief               qemu-virt platform main functions.
 */

#include <drivers/virtio/virtio.h>

#include <console.h>
#include <device.h>
#include <loader.h>
#include <memory.h>
#include <time.h>

/** Main function of the qemu-virt platform. */
extern void qemu_virt_main(void);

void qemu_virt_main(void) {
    console_init();

    arch_init();

    loader_main();
}

/** Detect and register all devices. */
void target_device_probe(void) {
}

/** Reboot the system. */
void target_reboot(void) {
    /* TODO: Call PSCI with SYSTEM_RESET */
    internal_error("Not implemented (reboot)");
}

/** Halt the system. */
__noreturn void target_halt(void) {
    __asm__ volatile("msr daifset, #2" ::: "memory");
    for(;;);
}

/** Get the current internal time.
 * @return              Current internal time. */
mstime_t current_time(void) {
    internal_error("Not implemented");
}
