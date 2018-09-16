/*
 * Copyright (C) 2017 Henry Harrington
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
 * @brief               orange-pi-pc2 platform main functions.
 */

#include <orange-pi-pc2/orange-pi-pc2.h>

#include <platform/fdt.h>

#include <arch/io.h>

#include <drivers/console/fb.h>
//#include <drivers/mmc/sunxi_mmc.h>
#include <drivers/virtio/virtio.h>

#include <console.h>
#include <device.h>
#include <loader.h>
#include <memory.h>
#include <time.h>
#include <libfdt.h>
#include <video.h>

const void *fdt_address;

static void print_fdt(void) {
    /* look for a flattened device tree at the start of physical memory */
    int err = fdt_check_header(fdt_address);
    if (err < 0) {
        dprintf("No device tree detected.\n");
        return;
    }
    dprintf("Device tree:\n");
    int depth = 0;
    int offset = 0;
    {
        int prop_offset = fdt_first_property_offset(fdt_address, 0);
        while(prop_offset >= 0) {
            const struct fdt_property *prop = fdt_get_property_by_offset(fdt_address, prop_offset, NULL);
            dprintf("  %s\n", fdt_string(fdt_address, fdt32_to_cpu(prop->nameoff)));
            prop_offset = fdt_next_property_offset(fdt_address, prop_offset);
        }
    }
    for (;;) {
        offset = fdt_next_node(fdt_address, offset, &depth);
        if (offset < 0)
            break;

        /* get the name */
        const char *name = fdt_get_name(fdt_address, offset, NULL);
        if (!name)
            continue;

        for(int i = 0; i < depth; i += 1) {
            dprintf("  ");
        }

        dprintf("  %s\n", name);

        int prop_offset = fdt_first_property_offset(fdt_address, offset);
        while(prop_offset >= 0) {
            int prop_len;
            const struct fdt_property *prop = fdt_get_property_by_offset(fdt_address, prop_offset, &prop_len);
            const char *name = fdt_string(fdt_address, fdt32_to_cpu(prop->nameoff));
            for(int i = 0; i < depth; i += 1) {
                dprintf("  ");
            }
            const char *data = prop->data;
            if(strcmp(name, "compatible") == 0 ||
               strcmp(name, "reset-names") == 0 ||
               strcmp(name, "clock-names") == 0) {
                const char *endp = data + prop_len;
                dprintf("    %s = ", name);
                bool first = true;
                while(data < endp) {
                    if(first) {
                        first = false;
                    } else {
                        dprintf(" ");
                    }
                    dprintf("\"%s\"", data);
                    data += strlen(data) + 1;
                }
                dprintf("\n");
            } else if(strcmp(name, "status") == 0 ||
                      strcmp(name, "stdout-path") == 0) {
                dprintf("    %s = %s\n", name, data);
            } else if(strcmp(name, "reg") == 0 ||
                      strcmp(name, "interrupts") == 0 ||
                      strcmp(name, "bus-width") == 0 ||
                      strcmp(name, "resets") == 0 ||
                      strcmp(name, "clocks") == 0 ||
                      strcmp(name, "phandle") == 0 ||
                      strcmp(name, "clock-frequency") == 0 ||
                      strcmp(name, "reg-shift") == 0 ||
                      strcmp(name, "reg-io-width") == 0 ||
                      name[0] == '#') {
                dprintf("    %s = <", name);
                for(int i = 0; i < prop_len / 4; i += 1) {
                    if(i != 0) {
                        dprintf(" ");
                    }
                    dprintf("0x%x", fdt32_to_cpu(((const uint32_t *)data)[i]));
                }
                dprintf(">\n");
            } else {
                dprintf("    %s\n", name);
            }
            prop_offset = fdt_next_property_offset(fdt_address, prop_offset);
        }
    }
}

typedef struct exception_regs {
    unsigned long elr, spsr;
    unsigned long x30, sp;
    unsigned long x28, x29;
    unsigned long x26, x27;
    unsigned long x24, x25;
    unsigned long x22, x23;
    unsigned long x20, x21;
    unsigned long x18, x19;
    unsigned long x16, x17;
    unsigned long x14, x15;
    unsigned long x12, x13;
    unsigned long x10, x11;
    unsigned long x8, x9;
    unsigned long x6, x7;
    unsigned long x4, x5;
    unsigned long x2, x3;
    unsigned long x0, x1;
} exception_regs_t;

void arm64_unhandled_sync_exception(exception_regs_t *regs);
void arm64_unhandled_sync_exception(exception_regs_t *regs) {
    dprintf("Unhandled synchronous exception.\n");
    dprintf("x0 =0x%016x x1 =0x%016x\n", regs->x0, regs->x1);
    dprintf("x2 =0x%016x x3 =0x%016x\n", regs->x2, regs->x3);
    dprintf("x4 =0x%016x x5 =0x%016x\n", regs->x4, regs->x5);
    dprintf("x6 =0x%016x x7 =0x%016x\n", regs->x6, regs->x7);
    dprintf("x8 =0x%016x x9 =0x%016x\n", regs->x8, regs->x9);
    dprintf("x10=0x%016x x11=0x%016x\n", regs->x10, regs->x11);
    dprintf("x12=0x%016x x13=0x%016x\n", regs->x12, regs->x13);
    dprintf("x14=0x%016x x15=0x%016x\n", regs->x14, regs->x15);
    dprintf("x16=0x%016x x17=0x%016x\n", regs->x16, regs->x17);
    dprintf("x18=0x%016x x19=0x%016x\n", regs->x18, regs->x19);
    dprintf("x20=0x%016x x21=0x%016x\n", regs->x20, regs->x21);
    dprintf("x22=0x%016x x23=0x%016x\n", regs->x22, regs->x23);
    dprintf("x24=0x%016x x25=0x%016x\n", regs->x24, regs->x25);
    dprintf("x26=0x%016x x27=0x%016x\n", regs->x26, regs->x27);
    dprintf("x28=0x%016x x29=0x%016x\n", regs->x28, regs->x29);
    dprintf("x30=0x%016x sp =0x%016x\n", regs->x30, regs->sp);
    dprintf("elr=0x%016x spsr=0x%016x\n", regs->elr, regs->spsr);

    unsigned long esr;
    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    dprintf("esr=0x%016x\n", esr);

    internal_error("Unhandled exception");
}

/** Main function of the orange-pi-pc2 platform. */
void orange_pi_pc2_main(void *provided_fdt) {
    for(int i = 0; i < 255; i++) {
        write8((volatile uint8_t *)0x01C28000, i);
    }

    fdt_address = provided_fdt;

    console_init();

    unsigned long currentel;
    asm volatile("mrs %0, CurrentEl" : "=r"(currentel));
    unsigned long el = (currentel >> 2) & 3;

    unsigned long sctlr;
    if(el == 2) {
        asm volatile("mrs %0, SCTLR_EL2" : "=r"(sctlr));
    } else {
        asm volatile("mrs %0, SCTLR_EL1" : "=r"(sctlr));
    }
    dprintf("Booted at EL%lu. SCTLR: %lx\n", el, sctlr);
    dprintf("Fdt at %x\n", fdt_address);

    arch_init();

    print_fdt();
    loader_main();
}

/** Check the status property of an FDT node.
 * Returns true if the status is "ok" or "okay" or if the property is not present. */
static bool check_fdt_node_status(const void *fdt, int node) {
    int len;
    const void *prop_ptr = fdt_getprop(fdt_address, node, "status", &len);
    if(prop_ptr == NULL) {
        return true;
    }

    if(len == 0) {
        // Present but invalid.
        return false;
    }
    if(strcmp(prop_ptr, "ok") == 0 || strcmp(prop_ptr, "okay") == 0) {
        return true;
    }
    return false;
}

static uint32_t read_n_cells_count(int fdt_node, const char *prop_name, uint32_t dflt) {
    for(;;) {
        int len;
        const void *prop_ptr = fdt_getprop(fdt_address, fdt_node, prop_name, &len);
        if(prop_ptr != NULL) {
            return fdt32_to_cpu(*((const uint32_t *)prop_ptr));
        }
        if(fdt_node == 0) {
            return dflt;
        }
        fdt_node = fdt_parent_offset(fdt_address, fdt_node);
        if(fdt_node < 0) {
            return dflt;
        }
    }
}

uint32_t platform_fdt_n_size_cells(int fdt_node) {
    return read_n_cells_count(fdt_node, "#size-cells", 1);
}

uint32_t platform_fdt_n_address_cells(int fdt_node) {
    return read_n_cells_count(fdt_node, "#address-cells", 1);
}

static uint64_t read_cell_value(const uint32_t *ptr, uint32_t n_cells) {
    uint64_t value = 0;
    for(uint32_t i = 0; i < n_cells; i += 1) {
        value = value << 32;
        value |= fdt32_to_cpu(*(ptr));
        ptr += 1;
    }
    return value;
}

uint64_t platform_fdt_get_value(int fdt_node, const char *name, uint64_t dflt) {
    int len;
    const void *prop_ptr = fdt_getprop(fdt_address, fdt_node, name, &len);
    if(prop_ptr == NULL) {
        return dflt;
    }

    if(len == 4) {
        return fdt32_to_cpu(*(const uint32_t *)prop_ptr);
    } else if(len == 8) {
        return fdt64_to_cpu(*(const uint64_t *)prop_ptr);
    } else {
        return dflt;
    }
}

void platform_fdt_get_reg(int fdt_node, int index, phys_ptr_t *address, phys_ptr_t *size) {
    uint32_t n_address_cells = platform_fdt_n_address_cells(fdt_node);
    uint32_t n_size_cells = platform_fdt_n_size_cells(fdt_node);

    int len;
    const void *prop_ptr = fdt_getprop(fdt_address, fdt_node, "reg", &len);
    int n_entries = len / 4 / (n_address_cells + n_size_cells);
    if(index >= n_entries) {
        *address = 0;
        *size = 0;
        return;
    }

    *address = read_cell_value(&((const uint32_t *)prop_ptr)[index * (n_address_cells + n_size_cells)], n_address_cells);
    *size = read_cell_value(&((const uint32_t *)prop_ptr)[index * (n_address_cells + n_size_cells) + n_address_cells], n_size_cells);
}

static void simplefb_set_mode(video_mode_t *_mode) {
}

/** Create a console for a mode.
 * @param _mode         Mode to create for.
 * @return              Pointer to created console. */
static console_out_t *simplefb_create_console(video_mode_t *_mode) {
    return fb_console_create();
}

/** Simplefb video operations. */
static video_ops_t simplefb_ops = {
    .set_mode = simplefb_set_mode,
    .create_console = simplefb_create_console,
};

static void simple_framebuffer_register(int node) {
    phys_ptr_t fb_base, fb_size;
    platform_fdt_get_reg(node, 0, &fb_base, &fb_size);
    uint32_t width = platform_fdt_get_value(node, "width", 0);
    uint32_t height = platform_fdt_get_value(node, "height", 0);
    uint32_t stride = platform_fdt_get_value(node, "stride", 0);
    const char *format = fdt_getprop(fdt_address, node, "format", NULL);

    dprintf("Detected %ux%u %s simplefb at %llx\n", width, height, format, fb_base);
    video_mode_t *mode = malloc(sizeof *mode);
    mode->type = VIDEO_MODE_LFB;
    mode->ops = &simplefb_ops;
    mode->width = width;
    mode->height = height;
    mode->mem_phys = fb_base;
    mode->mem_virt = fb_base;
    mode->mem_size = fb_size;
    mode->pitch = stride;
    if(strcmp(format, "r5g6b5") == 0) {
        mode->format.bpp = 16;
        mode->format.red_size = 5;
        mode->format.red_pos = 11;
        mode->format.green_size = 6;
        mode->format.green_pos = 5;
        mode->format.blue_size = 5;
        mode->format.blue_pos = 0;
        mode->format.alpha_size = 0;
        mode->format.alpha_pos = 0;
    } else if(strcmp(format, "a8b8g8r8") == 0) {
        // Seriously?
        mode->format.bpp = 32;
        mode->format.red_size = 8;
        mode->format.red_pos = 0;
        mode->format.green_size = 8;
        mode->format.green_pos = 8;
        mode->format.blue_size = 8;
        mode->format.blue_pos = 16;
        mode->format.alpha_size = 8;
        mode->format.alpha_pos = 24;
    } else if(strcmp(format, "x8r8g8b8") == 0) {
        mode->format.bpp = 32;
        mode->format.red_size = 8;
        mode->format.red_pos = 16;
        mode->format.green_size = 8;
        mode->format.green_pos = 8;
        mode->format.blue_size = 8;
        mode->format.blue_pos = 0;
        mode->format.alpha_size = 0;
        mode->format.alpha_pos = 0;
    } else {
        dprintf("Format not supported, ignoring.\n");
        free(mode);
        return;
    }

    video_mode_register(mode, true);
}

static void virtio_mmio_register(int node) {
    phys_ptr_t window_base, window_size;
    platform_fdt_get_reg(node, 0, &window_base, &window_size);
    virtio_mmio_detect((void *)window_base);
}

typedef void (*fdt_probe_fn_t)(int fdt_node);

typedef struct fdt_device_driver {
    const char *compatible;
    fdt_probe_fn_t probe;
} fdt_device_driver_t;

static fdt_device_driver_t fdt_drivers[] = {
    { "simple-framebuffer", simple_framebuffer_register },
    { "virtio,mmio", virtio_mmio_register },

    //{ "allwinner,sun50i-a64-mmc", sunxi_mmc_register },
};

/** Detect and register all devices. */
void target_device_probe(void) {
    for(int node = 0; node >= 0; node = fdt_next_node(fdt_address, node, NULL)) {
        for(size_t i = 0; i < array_size(fdt_drivers); i += 1) {
            int err = fdt_node_check_compatible(fdt_address, node, fdt_drivers[i].compatible);
            if(err == 0) {
                if(check_fdt_node_status(fdt_address, node)) {
                    fdt_drivers[i].probe(node);
                }
            } else if(!(err == 1 || err == -FDT_ERR_NOTFOUND)) {
                break;
            }
        }
    }

    /* Register the initrd as a virtual disk. */
    initrd_disk_init();
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
