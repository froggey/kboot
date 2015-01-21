/*
 * Copyright (C) 2014 Alex Smith
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
 * @brief               Disk device management.
 */

#include <lib/string.h>

#include <assert.h>
#include <disk.h>
#include <loader.h>
#include <memory.h>

/** Type of a partition disk device. */
typedef struct partition {
    disk_device_t disk;                 /**< Disk structure header. */

    disk_device_t *parent;              /**< Parent disk. */
    uint64_t offset;                    /**< Offset of the partition on the disk. */
} partition_t;

/** Next disk IDs. */
static uint8_t next_disk_ids[DISK_TYPE_FLOPPY + 1];

/** Disk type names. */
static const char *const disk_type_names[] = {
    [DISK_TYPE_HD] = "hd",
    [DISK_TYPE_CDROM] = "cdrom",
    [DISK_TYPE_FLOPPY] = "floppy",
};

/** Read from a disk.
 * @param device        Device to read from.
 * @param buf           Buffer to read into.
 * @param count         Number of bytes to read.
 * @param offset        Offset in the disk to read from.
 * @return              Whether the read was successful. */
status_t disk_device_read(device_t *device, void *buf, size_t count, offset_t offset) {
    disk_device_t *disk = (disk_device_t *)device;
    void *block __cleanup_free = NULL;
    uint64_t start, end, size;
    status_t ret;

    if (!count)
        return STATUS_SUCCESS;

    if ((uint64_t)(offset + count) > (disk->blocks * disk->block_size)) {
        dprintf("disk: requested read beyond end of disk (offset: %" PRId64 ", size: %zu, total: %" PRIu64 ")\n",
            offset, count, disk->blocks * disk->block_size);
        return STATUS_DEVICE_ERROR;
    }

    /* Allocate a temporary buffer for partial transfers if required. */
    if (offset % disk->block_size || count % disk->block_size)
        block = malloc(disk->block_size);

    /* Now work out the start block and the end block. Subtract one from count
     * to prevent end from going onto the next block when the offset plus the
     * count is an exact multiple of the block size. */
    start = offset / disk->block_size;
    end = (offset + (count - 1)) / disk->block_size;

    /* If we're not starting on a block boundary, we need to do a partial
     * transfer on the initial block to get up to a block boundary. If the
     * transfer only goes across one block, this will handle it. */
    if (offset % disk->block_size) {
        /* Read the block into the temporary buffer. */
        ret = disk->ops->read_blocks(disk, block, 1, start);
        if (ret != STATUS_SUCCESS)
            return ret;

        size = (start == end) ? count : disk->block_size - (size_t)(offset % disk->block_size);
        memcpy(buf, block + (offset % disk->block_size), size);
        buf += size;
        count -= size;
        start++;
    }

    /* Handle any full blocks. */
    size = count / disk->block_size;
    if (size) {
        ret = disk->ops->read_blocks(disk, buf, size, start);
        if (ret != STATUS_SUCCESS)
            return ret;

        buf += (size * disk->block_size);
        count -= (size * disk->block_size);
        start += size;
    }

    /* Handle anything that's left. */
    if (count > 0) {
        ret = disk->ops->read_blocks(disk, block, 1, start);
        if (ret != STATUS_SUCCESS)
            return ret;

        memcpy(buf, block, count);
    }

    return STATUS_SUCCESS;
}

/** Disk device operations. */
static device_ops_t disk_device_ops = {
    .read = disk_device_read,
};

/** Register a disk device.
 * @param disk          Disk device to register (details should be filled in). */
void disk_device_register(disk_device_t *disk) {
    char name[16];

    /* Assign an ID for the disk and name it. */
    disk->id = next_disk_ids[disk->type]++;
    snprintf(name, sizeof(name), "%s%u", disk_type_names[disk->type], disk->id);

    /* Add the device. */
    disk->device.type = DEVICE_TYPE_DISK;
    disk->device.ops = &disk_device_ops;
    device_register(&disk->device, name);
}

/** Read blocks from a partition.
 * @param disk          Disk being read from.
 * @param buf           Buffer to read into.
 * @param count         Number of blocks to read.
 * @param lba           Block number to start reading from.
 * @return              Status code describing the result of the operation. */
static status_t partition_read_blocks(disk_device_t *disk, void *buf, size_t count, uint64_t lba) {
    partition_t *partition = (partition_t *)disk;

    return partition->parent->ops->read_blocks(partition->parent, buf, count, lba + partition->offset);
}

/** Operations for a partition. */
static disk_ops_t partition_disk_ops = {
    .read_blocks = partition_read_blocks,
};

/** Add a partition to a disk device.
 * @param parent        Parent of the partition.
 * @param id            ID of the partition.
 * @param lba           Start LBA.
 * @param blocks        Size in blocks. */
static void add_partition(disk_device_t *parent, uint8_t id, uint64_t lba, uint64_t blocks) {
    partition_t *partition;
    char name[32];

    partition = malloc(sizeof(*partition));
    partition->disk.device.type = DEVICE_TYPE_DISK;
    partition->disk.device.ops = &disk_device_ops;
    partition->disk.type = parent->type;
    partition->disk.ops = &partition_disk_ops;
    partition->disk.blocks = blocks;
    partition->disk.block_size = parent->block_size;
    partition->disk.id = id;
    partition->parent = parent;
    partition->offset = lba;

    snprintf(name, sizeof(name), "%s,%u", parent->device.name, id);

    device_register(&partition->disk.device, name);

    /* Probe for filesystems/partitions. */
    disk_device_probe(&partition->disk);
}

/** Probe a disk device's contents.
 * @param disk          Disk device to probe. */
void disk_device_probe(disk_device_t *disk) {
    if (!disk->blocks)
        return;

    /* Check for a partition table on the device. */
    builtin_foreach(BUILTIN_TYPE_PARTITION, partition_ops_t, ops) {
        if (ops->iterate(disk, add_partition))
            return;
    }
}