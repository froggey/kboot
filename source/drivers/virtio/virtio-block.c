// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <drivers/virtio/virtio.h>

#include <lib/string.h>
#include <lib/utility.h>

#include <assert.h>
#include <compiler.h>
#include <disk.h>
#include <memory.h>

#define LOCAL_TRACE 0

struct virtio_blk_config {
    uint64_t capacity;
    uint32_t size_max;
    uint32_t seg_max;
    struct virtio_blk_geometry {
        uint16_t cylinders;
        uint8_t heads;
        uint8_t sectors;
    } geometry;
    uint32_t blk_size;
} __packed;

struct virtio_blk_req {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector;
} __packed;

#define VIRTIO_BLK_F_BARRIER  (1<<0)
#define VIRTIO_BLK_F_SIZE_MAX (1<<1)
#define VIRTIO_BLK_F_SEG_MAX  (1<<2)
#define VIRTIO_BLK_F_GEOMETRY (1<<4)
#define VIRTIO_BLK_F_RO       (1<<5)
#define VIRTIO_BLK_F_BLK_SIZE (1<<6)
#define VIRTIO_BLK_F_SCSI     (1<<7)
#define VIRTIO_BLK_F_FLUSH    (1<<9)
#define VIRTIO_BLK_F_TOPOLOGY (1<<10)
#define VIRTIO_BLK_F_CONFIG_WCE (1<<11)

#define VIRTIO_BLK_T_IN         0
#define VIRTIO_BLK_T_OUT        1
#define VIRTIO_BLK_T_FLUSH      4

#define VIRTIO_BLK_S_OK         0
#define VIRTIO_BLK_S_IOERR      1
#define VIRTIO_BLK_S_UNSUPP     2

typedef struct virtio_block_disk {
    disk_device_t disk;
    struct virtio_device *dev;

    /* one blk_req structure for io, not crossing a page boundary */
    struct virtio_blk_req *blk_req;

    /* one uint8_t response word */
    uint8_t blk_response;
} virtio_block_disk_t;

#if 0
static void virtio_block_irq_driver_callback(struct virtio_device *dev, unsigned int ring, const struct vring_used_elem *e)
{
    struct virtio_block_dev *bdev = (struct virtio_block_dev *)dev->priv;

    LTRACEF("dev %p, ring %u, e %p, id %u, len %u\n", dev, ring, e, e->id, e->len);

    /* parse our descriptor chain, add back to the free queue */
    uint16_t i = e->id;
    for (;;) {
        int next;
        struct vring_desc *desc = virtio_desc_index_to_desc(dev, ring, i);

        //virtio_dump_desc(desc);

        if (desc->flags & VRING_DESC_F_NEXT) {
            next = desc->next;
        } else {
            /* end of chain */
            next = -1;
        }

        virtio_free_desc(dev, ring, i);

        if (next < 0)
            break;
        i = next;
    }

    /* signal our event */
    event_signal(&bdev->io_event, false);
}
#endif

static status_t virtio_block_disk_read_blocks(disk_device_t *_disk, void *buf, size_t count, uint64_t block) {
    virtio_block_disk_t *bdev = (virtio_block_disk_t *)_disk;
    struct virtio_device *dev = bdev->dev;

    LTRACEF("dev %p, buf %p, block 0x%x, count %u\n", bdev, buf, block, count);

    uint16_t i;
    struct vring_desc *desc;

    /* set up the request */
    bdev->blk_req->type = VIRTIO_BLK_T_IN;
    bdev->blk_req->ioprio = 0;
    bdev->blk_req->sector = block;
    LTRACEF("blk_req type %u ioprio %u sector %llu\n",
            bdev->blk_req->type, bdev->blk_req->ioprio, bdev->blk_req->sector);

    /* put together a transfer */
    desc = virtio_alloc_desc_chain(dev, 0, 3, &i);
    LTRACEF("after alloc chain desc %p, i %u\n", desc, i);

    // XXX not cache safe.
    // At the moment only tested on arm qemu, which doesn't emulate cache.

    /* set up the descriptor pointing to the head */
    desc->addr = (phys_ptr_t)bdev->blk_req;
    desc->len = sizeof(struct virtio_blk_req);
    desc->flags |= VRING_DESC_F_NEXT;

    /* set up the descriptor pointing to the buffer */
    desc = virtio_desc_index_to_desc(dev, 0, desc->next);
    desc->addr = (uint64_t)(uintptr_t)buf;
    desc->len = count * 512; /* ### block size */
    desc->flags |= VRING_DESC_F_WRITE; /* block read, mark buffer as write-only */
    desc->flags |= VRING_DESC_F_NEXT;

    /* set up the descriptor pointing to the response */
    desc = virtio_desc_index_to_desc(dev, 0, desc->next);
    desc->addr = (phys_ptr_t)&bdev->blk_response;
    desc->len = 1;
    desc->flags = VRING_DESC_F_WRITE;

    /* submit the transfer */
    virtio_submit_chain(dev, 0, i);

    /* kick it off */
    virtio_kick(dev, 0);

    /* wait for the transfer to complete */
    virtio_irq_wait(dev);

    /* parse our descriptor chain, add back to the free queue */
    uint16_t j = i;
    for (;;) {
        int next;
        struct vring_desc *desc = virtio_desc_index_to_desc(dev, 0, j);

        //virtio_dump_desc(desc);

        if (desc->flags & VRING_DESC_F_NEXT) {
            next = desc->next;
        } else {
            /* end of chain */
            next = -1;
        }

        virtio_free_desc(dev, 0, j);

        if (next < 0)
            break;
        j = next;
    }

    LTRACEF("status 0x%x\n", bdev->blk_response);

    return STATUS_SUCCESS;
}

static void virtio_block_disk_identify(disk_device_t *_disk, device_identify_t type, char *buf, size_t size) {
    virtio_block_disk_t *disk = (virtio_block_disk_t *)_disk;

    if (type == DEVICE_IDENTIFY_SHORT)
        snprintf(buf, size, "Virtio-block disk %u", disk->dev->index);
}

static bool virtio_block_disk_is_boot_partition(disk_device_t *disk, uint8_t id, uint64_t lba) {
    /* Close enough. */
    return id == 1;
}

/** Operations for a virtio-block disk device. */
static const disk_ops_t virtio_block_disk_ops = {
    .read_blocks = virtio_block_disk_read_blocks,
    .is_boot_partition = virtio_block_disk_is_boot_partition,
    .identify = virtio_block_disk_identify,
};

status_t virtio_block_init(struct virtio_device *dev, uint32_t host_features)
{
    LTRACEF("dev %p, host_features 0x%x\n", dev, host_features);

    /* allocate a new block device */
    struct virtio_block_disk *bdev = malloc(sizeof(struct virtio_block_disk));
    if (!bdev)
        return STATUS_NO_MEMORY;

    bdev->disk.ops = &virtio_block_disk_ops;
    bdev->dev = dev;

    // Use malloc_large as it gives aligned allocations.
    bdev->blk_req = malloc_large(sizeof(struct virtio_blk_req));
    LTRACEF("blk_req structure at %p\n", bdev->blk_req);

    /* make sure the device is reset */
    virtio_reset_device(dev);

    volatile struct virtio_blk_config *config = (struct virtio_blk_config *)dev->config_ptr;

    LTRACEF("capacity 0x%llx\n", config->capacity);
    LTRACEF("size_max 0x%x\n", config->size_max);
    LTRACEF("seg_max  0x%x\n", config->seg_max);
    LTRACEF("blk_size 0x%x\n", config->blk_size);

    /* ack and set the driver status bit */
    virtio_status_acknowledge_driver(dev);

    // XXX check features bits and ack/nak them

    /* allocate a virtio ring */
    status_t err = virtio_alloc_ring(dev, 0, 256);
    if (err < 0) {
        dprintf("failed to allocate virtio ring\n");
        free_large(bdev->blk_req);
        free(bdev);
        return err;
    }

    /* set DRIVER_OK */
    virtio_status_driver_ok(dev);

    printf("found virtio block device of size %lld\n", config->capacity * config->blk_size);

    bdev->disk.type = DISK_TYPE_HD;
    bdev->disk.block_size = config->blk_size;
    bdev->disk.blocks = config->capacity;
    disk_device_register(&bdev->disk, true); /* Yeah, this is totally the boot device. */

    return STATUS_SUCCESS;
}
