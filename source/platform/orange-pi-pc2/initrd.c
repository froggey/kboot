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
 * @brief               Initrd virtual disk.
 */

#include <orange-pi-pc2/orange-pi-pc2.h>

#include <lib/string.h>
#include <lib/utility.h>

#include <fs/decompress.h>

#include <fs.h>
#include <disk.h>
#include <types.h>
#include <loader.h>
#include <memory.h>

#define INITRD_BLOCK_SIZE 512

phys_ptr_t initrd_address = 0;
phys_ptr_t initrd_size = 0;

static status_t initrd_fs_read(struct fs_handle *_handle, void *buf, size_t count, offset_t offset) {
    memcpy(buf, (const void *)(initrd_address + offset), count);
    return STATUS_SUCCESS;
}

static fs_ops_t fake_initrd_fs_ops = {
    .read = initrd_fs_read,
};

static fs_mount_t fake_initrd_mount = {
    .ops = &fake_initrd_fs_ops,
};

static fs_handle_t fake_initrd_file;
static fs_handle_t *compressed_initrd_file;

static status_t initrd_disk_read_blocks(disk_device_t *_disk, void *buf, size_t count, uint64_t block) {
    if (compressed_initrd_file) {
        return fs_read(compressed_initrd_file, buf, count * INITRD_BLOCK_SIZE, block * INITRD_BLOCK_SIZE);
    } else {
        return fs_read(&fake_initrd_file, buf, count * INITRD_BLOCK_SIZE, block * INITRD_BLOCK_SIZE);
    }
}

static bool initrd_disk_is_boot_partition(disk_device_t *disk, uint8_t id, uint64_t lba) {
    /* Close enough. Partition 1, same as virtio. */
    return id == 1;
}

static void initrd_disk_identify(disk_device_t *_disk, device_identify_t type, char *buf, size_t size) {
    if (type == DEVICE_IDENTIFY_SHORT)
        snprintf(buf, size, "Initrd disk");
}

static disk_ops_t initrd_disk_ops = {
    .read_blocks = initrd_disk_read_blocks,
    .is_boot_partition = initrd_disk_is_boot_partition,
    .identify = initrd_disk_identify,
};

void initrd_disk_init(void) {
    if(initrd_address == 0 || initrd_size == 0) {
        return;
    }

    fs_handle_init(&fake_initrd_file, &fake_initrd_mount, FILE_TYPE_REGULAR, initrd_size);

    if(!decompress_open(&fake_initrd_file, &compressed_initrd_file)) {
        dprintf("initrd: %llu byte uncompressed initrd at %llx\n", initrd_size, initrd_address);
        compressed_initrd_file = NULL;
    } else {
        dprintf("initrd: %llu byte compressed initrd at %llx\n", compressed_initrd_file->size, initrd_address);
    }

    disk_device_t *disk = malloc(sizeof(disk_device_t));
    disk->ops = &initrd_disk_ops;
    disk->type = DISK_TYPE_HD;
    disk->block_size = INITRD_BLOCK_SIZE;
    if(compressed_initrd_file) {
        disk->blocks = compressed_initrd_file->size / INITRD_BLOCK_SIZE;
    } else {
        disk->blocks = initrd_size / INITRD_BLOCK_SIZE;
    }
    disk_device_register(disk, true);
}
