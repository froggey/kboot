// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014-2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <drivers/virtio/virtio.h>
#include <drivers/virtio/ring.h>

#include <assert.h>
#include <compiler.h>
#include <memory.h>
#include <lib/string.h>

#include <arch/barrier.h>

#include "virtio_priv.h"

static inline bool ispow2(unsigned int val)
{
    return ((val - 1) & val) == 0;
}

#define LOCAL_TRACE 0

static struct virtio_device *devices;

#if LOCAL_TRACE
static void dump_mmio_config(const volatile struct virtio_mmio_config *mmio)
{
    dprintf("mmio at %p\n", mmio);
    dprintf("\tmagic 0x%x\n", mmio->magic);
    dprintf("\tversion 0x%x\n", mmio->version);
    dprintf("\tdevice_id 0x%x\n", mmio->device_id);
    dprintf("\tvendor_id 0x%x\n", mmio->vendor_id);
    dprintf("\thost_features 0x%x\n", mmio->host_features);
    dprintf("\tguest_page_size %u\n", mmio->guest_page_size);
    dprintf("\tqnum %u\n", mmio->queue_num);
    dprintf("\tqnum_max %u\n", mmio->queue_num_max);
    dprintf("\tqnum_align %u\n", mmio->queue_align);
    dprintf("\tqnum_pfn %u\n", mmio->queue_pfn);
    dprintf("\tstatus 0x%x\n", mmio->status);
}
#endif

void virtio_dump_desc(const struct vring_desc *desc)
{
    dprintf("vring descriptor %p\n", desc);
    dprintf("\taddr  0x%llx\n", desc->addr);
    dprintf("\tlen   0x%x\n", desc->len);
    dprintf("\tflags 0x%hhx\n", desc->flags);
    dprintf("\tnext  0x%hhx\n", desc->next);
}

int virtio_mmio_detect(void *ptr, unsigned int count)
{
    static_assert(sizeof(struct virtio_mmio_config) == 0x100);

    LTRACEF("ptr %p, count %u\n", ptr, count);

    assert(ptr);
    assert(!devices);

    /* allocate an array big enough to hold a list of devices */
    devices = malloc(count * sizeof(struct virtio_device));
    if (!devices)
        return STATUS_NO_MEMORY;
    memset(devices, 0, count * sizeof(struct virtio_device));

    int found = 0;
    for (unsigned int i = 0; i < count; i++) {
        volatile struct virtio_mmio_config *mmio = (struct virtio_mmio_config *)((uint8_t *)ptr + i * 0x200);
        struct virtio_device *dev = &devices[i];

        dev->index = i;

        LTRACEF("looking at magic 0x%x version 0x%x did 0x%x vid 0x%x\n",
                mmio->magic, mmio->version, mmio->device_id, mmio->vendor_id);

        if ((mmio->magic != VIRTIO_MMIO_MAGIC) ||
            (mmio->device_id == VIRTIO_DEV_ID_INVALID))
            continue;

#if LOCAL_TRACE
        dump_mmio_config(mmio);
#endif

        dev->mmio_config = mmio;
        dev->config_ptr = (void *)mmio->config;

        status_t status = STATUS_NOT_SUPPORTED;
        switch(mmio->device_id) {
#ifdef CONFIG_DRIVER_VIRTIO_BLOCK
        case VIRTIO_DEV_ID_BLOCK:
            status = virtio_block_init(dev, mmio->host_features);
            break;
#endif
        default:
            dprintf("Unrecognized VirtIO MMIO device id %u discovered at position %u\n",
                    mmio->device_id, i);
            break;
        }

        if(status == STATUS_SUCCESS) {
            dev->valid = true;
            found++;
        } else if(status != STATUS_NOT_SUPPORTED) {
                LTRACEF("Failed to initialize VirtIO MMIO device id %u at position %u (err = %d)\n",
                        mmio->device_id, i, status);

                // indicate to the device that something went fatally wrong on the driver side.
                dev->mmio_config->status |= VIRTIO_STATUS_FAILED;
        }
    }

    return found;
}

void virtio_free_desc(struct virtio_device *dev, unsigned int ring_index, uint16_t desc_index)
{
    LTRACEF("dev %p ring %u index %u free_count %u\n", dev, ring_index, desc_index, dev->ring[ring_index].free_count);
    dev->ring[ring_index].desc[desc_index].next = dev->ring[ring_index].free_list;
    dev->ring[ring_index].free_list = desc_index;
    dev->ring[ring_index].free_count++;
}

void virtio_free_desc_chain(struct virtio_device *dev, unsigned int ring_index, uint16_t chain_head)
{
    struct vring_desc* desc = virtio_desc_index_to_desc(dev, ring_index, chain_head);

    while (desc->flags & VRING_DESC_F_NEXT) {
        uint16_t next = desc->next;
        virtio_free_desc(dev, ring_index, chain_head);
        chain_head = next;
        desc = virtio_desc_index_to_desc(dev, ring_index, chain_head);
    }

    virtio_free_desc(dev, ring_index, chain_head);
}

uint16_t virtio_alloc_desc(struct virtio_device *dev, unsigned int ring_index)
{
    if (dev->ring[ring_index].free_count == 0)
        return 0xffff;

    assert(dev->ring[ring_index].free_list != 0xffff);

    uint16_t i = dev->ring[ring_index].free_list;
    struct vring_desc *desc = &dev->ring[ring_index].desc[i];
    dev->ring[ring_index].free_list = desc->next;

    dev->ring[ring_index].free_count--;

    return i;
}

struct vring_desc *virtio_alloc_desc_chain(struct virtio_device *dev, unsigned int ring_index, size_t count, uint16_t *start_index)
{
    if (dev->ring[ring_index].free_count < count)
        return NULL;

    /* start popping entries off the chain */
    struct vring_desc *last = 0;
    uint16_t last_index = 0;
    while (count > 0) {
        uint16_t i = dev->ring[ring_index].free_list;
        struct vring_desc *desc = &dev->ring[ring_index].desc[i];

        dev->ring[ring_index].free_list = desc->next;
        dev->ring[ring_index].free_count--;

        if (last) {
            desc->flags = VRING_DESC_F_NEXT;
            desc->next = last_index;
        } else {
            // first one
            desc->flags = 0;
            desc->next = 0;
        }
        last = desc;
        last_index = i;
        count--;
    }

    if (start_index)
        *start_index = last_index;

    return last;
}

void virtio_submit_chain(struct virtio_device *dev, unsigned int ring_index, uint16_t desc_index)
{
    LTRACEF("dev %p, ring %u, desc %u\n", dev, ring_index, desc_index);

    /* add the chain to the available list */
    struct vring_avail *avail = dev->ring[ring_index].avail;

    avail->ring[avail->idx & dev->ring[ring_index].num_mask] = desc_index;
    DSB;
    avail->idx++;

#if LOCAL_TRACE
    //hexdump(avail, 16);
#endif
}

void virtio_kick(struct virtio_device *dev, unsigned int ring_index)
{
    LTRACEF("dev %p, ring %u\n", dev, ring_index);

    dev->mmio_config->queue_notify = ring_index;
    DSB;
}

status_t virtio_alloc_ring(struct virtio_device *dev, unsigned int index, uint16_t len)
{
    LTRACEF("dev %p, index %u, len %u\n", dev, index, len);

    assert(dev);
    assert(len > 0 && ispow2(len));
    assert(index < MAX_VIRTIO_RINGS);

    if (len == 0 || !ispow2(len))
        return STATUS_INVALID_ARG;

    struct vring *ring = &dev->ring[index];

    /* allocate a ring */
    size_t size = vring_size(len, PAGE_SIZE);
    LTRACEF("need %zu bytes\n", size);

    void *vptr = malloc_large(size);
    if (!vptr)
        return STATUS_NO_MEMORY;

    LTRACEF("ptr %p\n", vptr);
    memset(vptr, 0, size);

    /* compute the physical address */
    phys_ptr_t pa = (phys_ptr_t)vptr;

    /* initialize the ring */
    vring_init(ring, len, vptr, PAGE_SIZE);
    dev->ring[index].free_list = 0xffff;
    dev->ring[index].free_count = 0;

    /* add all the descriptors to the free list */
    for (unsigned int i = 0; i < len; i++) {
        virtio_free_desc(dev, index, i);
    }

    /* register the ring with the device */
    assert(dev->mmio_config);
    dev->mmio_config->guest_page_size = PAGE_SIZE;
    dev->mmio_config->queue_sel = index;
    dev->mmio_config->queue_num = len;
    dev->mmio_config->queue_align = PAGE_SIZE;
    dev->mmio_config->queue_pfn = pa / PAGE_SIZE;

    /* mark the ring active */
    dev->active_rings_bitmap |= (1 << index);

    return STATUS_SUCCESS;
}

void virtio_reset_device(struct virtio_device *dev)
{
    dev->mmio_config->status = 0;
}

void virtio_status_acknowledge_driver(struct virtio_device *dev)
{
    dev->mmio_config->status |= VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;
}

void virtio_status_driver_ok(struct virtio_device *dev)
{
    dev->mmio_config->status |= VIRTIO_STATUS_DRIVER_OK;
}

void virtio_irq_wait(struct virtio_device *dev)
{
    LTRACEF("irq wait dev %p, index %u\n", dev, dev->index);

    while((dev->mmio_config->interrupt_status & 0x1) == 0) {
        arch_pause();
    }

    dev->mmio_config->interrupt_ack = 0x1;
}
