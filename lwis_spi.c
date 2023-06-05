// SPDX-License-Identifier: GPL-2.0
/*
 * Google LWIS SPI Interface
 *
 * Copyright (c) 2023 Google, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define pr_fmt(fmt) KBUILD_MODNAME "-spi: " fmt

#include "lwis_spi.h"

#include <linux/bits.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>

/* Max bit width for register and data that is supported by this
   driver currently */
#define MIN_OFFSET_BITS 8
#define MAX_OFFSET_BITS 16
#define MIN_DATA_BITS 8
#define MAX_DATA_BITS 32

static inline bool check_bitwidth(const int bitwidth, const int min, const int max)
{
	return (bitwidth >= min) && (bitwidth <= max) && ((bitwidth % 8) == 0);
}

/* value_to_buf(): SPI uses MSB, convert value to MSB buffer. */
static void value_to_buf(uint64_t value, uint8_t *buf, int buf_size)
{
	if (buf_size == 1) {
		buf[0] = value;
	} else if (buf_size == 2) {
		buf[0] = (value >> 8) & 0xFF;
		buf[1] = value & 0xFF;
	} else if (buf_size == 4) {
		buf[0] = (value >> 24) & 0xFF;
		buf[1] = (value >> 16) & 0xFF;
		buf[2] = (value >> 8) & 0xFF;
		buf[3] = value & 0xFF;
	} else {
		pr_err("Unsupported buffer size %d used for value_to_buf\n", buf_size);
	}
}

/* buf_to_value(): SPI uses MSB, convert MSB buffer to value. */
static uint64_t buf_to_value(uint8_t *buf, int buf_size)
{
	if (buf_size == 1) {
		return buf[0];
	} else if (buf_size == 2) {
		return (buf[0] << 8) | buf[1];
	} else if (buf_size == 4) {
		return (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
	}

	pr_err("Unsupported buffer size %d used for buf_to_value\n", buf_size);
	return 0;
}

static int lwis_spi_read(struct lwis_spi_device *spi_dev, uint64_t offset, uint64_t *value)
{
	int ret = 0;
	unsigned int offset_bits;
	unsigned int offset_bytes;
	uint64_t offset_overflow_value;
	unsigned int value_bits;
	unsigned int value_bytes;
	u32 rbuf = 0;
	u32 wbuf = 0;
	struct spi_message msg;
	struct spi_transfer tx;
	struct spi_transfer rx;
	unsigned long flags;

	if (!spi_dev || !spi_dev->spi) {
		pr_err("Cannot find SPI instance\n");
		return -ENODEV;
	}

	offset_bits = spi_dev->base_dev.native_addr_bitwidth;
	offset_bytes = offset_bits / BITS_PER_BYTE;
	if (!check_bitwidth(offset_bits, MIN_OFFSET_BITS, MAX_OFFSET_BITS)) {
		dev_err(spi_dev->base_dev.dev, "Invalid offset bitwidth %d\n", offset_bits);
		return -EINVAL;
	}

	value_bits = spi_dev->base_dev.native_value_bitwidth;
	value_bytes = value_bits / BITS_PER_BYTE;
	if (!check_bitwidth(value_bits, MIN_DATA_BITS, MAX_DATA_BITS)) {
		dev_err(spi_dev->base_dev.dev, "Invalid value bitwidth %d\n", value_bits);
		return -EINVAL;
	}

	offset_overflow_value = 1 << (offset_bits - 1);
	if (offset >= offset_overflow_value) {
		dev_err(spi_dev->base_dev.dev, "Max offset is %d bits\n", offset_bits - 1);
		return -EINVAL;
	}

	spi_message_init(&msg);

	memset(&tx, 0, sizeof(tx));
	value_to_buf(offset, (uint8_t *)&wbuf, offset_bytes);
	tx.len = offset_bytes;
	tx.tx_buf = &wbuf;
	spi_message_add_tail(&tx, &msg);

	memset(&rx, 0, sizeof(rx));
	rx.len = value_bytes;
	rx.rx_buf = &rbuf;
	spi_message_add_tail(&rx, &msg);

	spin_lock_irqsave(&spi_dev->spi_lock, flags);
	ret = spi_sync(spi_dev->spi, &msg);
	spin_unlock_irqrestore(&spi_dev->spi_lock, flags);
	if (ret < 0) {
		dev_err(spi_dev->base_dev.dev, "spi_sync() error:%d\n", ret);
		return ret;
	}

	*value = buf_to_value((uint8_t *)&rbuf, value_bytes);

	return ret;
}

static int lwis_spi_write(struct lwis_spi_device *spi_dev, uint64_t offset, uint64_t value)
{
	int ret = 0;
	unsigned int offset_bits;
	unsigned int offset_bytes;
	uint64_t offset_overflow_value;
	unsigned int value_bits;
	unsigned int value_bytes;
	uint64_t value_overflow_value;
	uint64_t wbuf_val = 0;
	uint8_t *wbuf;
	struct spi_message msg;
	struct spi_transfer tx;
	unsigned long flags;

	if (!spi_dev || !spi_dev->spi) {
		pr_err("Cannot find SPI instance\n");
		return -ENODEV;
	}

	if (spi_dev->base_dev.is_read_only) {
		dev_err(spi_dev->base_dev.dev, "Device is read only\n");
		return -EPERM;
	}

	offset_bits = spi_dev->base_dev.native_addr_bitwidth;
	offset_bytes = offset_bits / BITS_PER_BYTE;
	if (!check_bitwidth(offset_bits, MIN_OFFSET_BITS, MAX_OFFSET_BITS)) {
		dev_err(spi_dev->base_dev.dev, "Invalid offset bitwidth %d\n", offset_bits);
		return -EINVAL;
	}

	value_bits = spi_dev->base_dev.native_value_bitwidth;
	value_bytes = value_bits / BITS_PER_BYTE;
	if (!check_bitwidth(value_bits, MIN_DATA_BITS, MAX_DATA_BITS)) {
		dev_err(spi_dev->base_dev.dev, "Invalid value bitwidth %d\n", value_bits);
		return -EINVAL;
	}

	offset_overflow_value = 1 << (offset_bits - 1);
	if (offset >= offset_overflow_value) {
		dev_err(spi_dev->base_dev.dev, "Max offset is %d bits\n", offset_bits - 1);
		return -EINVAL;
	}

	value_overflow_value = 1 << value_bits;
	if (value >= value_overflow_value) {
		dev_err(spi_dev->base_dev.dev, "Max value is %d bits\n", value_bits);
		return -EINVAL;
	}

	spi_message_init(&msg);

	wbuf = (uint8_t *)&wbuf_val;
	memset(&tx, 0, sizeof(struct spi_transfer));
	offset |= offset_overflow_value;
	value_to_buf(offset, wbuf, offset_bytes);
	value_to_buf(value, wbuf + offset_bytes, value_bytes);
	tx.len = offset_bytes + value_bytes;
	tx.tx_buf = wbuf;
	spi_message_add_tail(&tx, &msg);

	spin_lock_irqsave(&spi_dev->spi_lock, flags);
	ret = spi_sync(spi_dev->spi, &msg);
	spin_unlock_irqrestore(&spi_dev->spi_lock, flags);
	if (ret < 0) {
		dev_err(spi_dev->base_dev.dev, "spi_sync() error:%d\n", ret);
	}

	return ret;
}

static int lwis_spi_read_batch(struct lwis_spi_device *spi_dev, uint64_t offset, uint8_t *read_buf,
			       int read_buf_size)
{
	int ret = 0;
	unsigned int offset_bits;
	unsigned int offset_bytes;
	uint64_t offset_overflow_value;
	u32 wbuf = 0;
	struct spi_message msg;
	struct spi_transfer tx;
	struct spi_transfer rx;
	unsigned long flags;

	if (!spi_dev || !spi_dev->spi) {
		pr_err("Cannot find SPI instance\n");
		return -ENODEV;
	}

	offset_bits = spi_dev->base_dev.native_addr_bitwidth;
	offset_bytes = offset_bits / BITS_PER_BYTE;
	if (!check_bitwidth(offset_bits, MIN_OFFSET_BITS, MAX_OFFSET_BITS)) {
		dev_err(spi_dev->base_dev.dev, "Invalid offset bitwidth %d\n", offset_bits);
		return -EINVAL;
	}

	offset_overflow_value = 1 << (offset_bits - 1);
	if (offset >= offset_overflow_value) {
		dev_err(spi_dev->base_dev.dev, "Max offset is %d bits\n", offset_bits - 1);
		return -EINVAL;
	}

	spi_message_init(&msg);

	memset(&tx, 0, sizeof(tx));
	value_to_buf(offset, (uint8_t *)&wbuf, offset_bytes);
	tx.len = offset_bytes;
	tx.tx_buf = &wbuf;
	spi_message_add_tail(&tx, &msg);

	memset(&rx, 0, sizeof(rx));
	rx.len = read_buf_size;
	rx.rx_buf = read_buf;
	spi_message_add_tail(&rx, &msg);

	spin_lock_irqsave(&spi_dev->spi_lock, flags);
	ret = spi_sync(spi_dev->spi, &msg);
	spin_unlock_irqrestore(&spi_dev->spi_lock, flags);
	if (ret < 0) {
		dev_err(spi_dev->base_dev.dev, "spi_sync() error:%d\n", ret);
		return ret;
	}

	return ret;
}

static int lwis_spi_write_batch(struct lwis_spi_device *spi_dev, uint64_t offset,
				uint8_t *write_buf, int write_buf_size)
{
	int ret = 0;
	unsigned int offset_bits;
	unsigned int offset_bytes;
	uint64_t offset_overflow_value;
	uint8_t *buf;
	int msg_bytes;
	struct spi_message msg;
	struct spi_transfer tx;
	unsigned long flags;

	if (!spi_dev || !spi_dev->spi) {
		pr_err("Cannot find SPI instance\n");
		return -ENODEV;
	}

	if (spi_dev->base_dev.is_read_only) {
		dev_err(spi_dev->base_dev.dev, "Device is read only\n");
		return -EPERM;
	}

	offset_bits = spi_dev->base_dev.native_addr_bitwidth;
	offset_bytes = offset_bits / BITS_PER_BYTE;
	if (!check_bitwidth(offset_bits, MIN_OFFSET_BITS, MAX_OFFSET_BITS)) {
		dev_err(spi_dev->base_dev.dev, "Invalid offset bitwidth %d\n", offset_bits);
		return -EINVAL;
	}

	offset_overflow_value = 1 << (offset_bits - 1);
	if (offset >= offset_overflow_value) {
		dev_err(spi_dev->base_dev.dev, "Max offset is %d bits\n", offset_bits - 1);
		return -EINVAL;
	}

	msg_bytes = offset_bytes + write_buf_size;
	buf = kmalloc(msg_bytes, GFP_KERNEL);
	if (!buf) {
		dev_err(spi_dev->base_dev.dev, "Failed to allocate memory for SPI buffer\n");
		return -ENOMEM;
	}

	spi_message_init(&msg);

	memset(&tx, 0, sizeof(struct spi_transfer));
	offset |= offset_overflow_value;
	value_to_buf(offset, buf, offset_bytes);
	memcpy(buf + offset_bytes, write_buf, write_buf_size);
	tx.len = msg_bytes;
	tx.tx_buf = buf;
	spi_message_add_tail(&tx, &msg);

	spin_lock_irqsave(&spi_dev->spi_lock, flags);
	ret = spi_sync(spi_dev->spi, &msg);
	spin_unlock_irqrestore(&spi_dev->spi_lock, flags);
	if (ret < 0) {
		dev_err(spi_dev->base_dev.dev, "spi_sync() error:%d\n", ret);
	}

	kfree(buf);
	return ret;
}

int lwis_spi_io_entry_rw(struct lwis_spi_device *spi_dev, struct lwis_io_entry *entry)
{
	if (!spi_dev || !spi_dev->spi) {
		pr_err("Cannot find SPI instance\n");
		return -ENODEV;
	}
	if (!entry) {
		dev_err(spi_dev->base_dev.dev, "IO entry is NULL.\n");
		return -EINVAL;
	}

	if (entry->type == LWIS_IO_ENTRY_READ) {
		return lwis_spi_read(spi_dev, entry->rw.offset, &entry->rw.val);
	}
	if (entry->type == LWIS_IO_ENTRY_WRITE) {
		return lwis_spi_write(spi_dev, entry->rw.offset, entry->rw.val);
	}
	if (entry->type == LWIS_IO_ENTRY_MODIFY) {
		int ret;
		uint64_t reg_value;
		ret = lwis_spi_read(spi_dev, entry->mod.offset, &reg_value);
		if (ret) {
			return ret;
		}
		reg_value &= ~entry->mod.val_mask;
		reg_value |= entry->mod.val_mask & entry->mod.val;
		return lwis_spi_write(spi_dev, entry->mod.offset, reg_value);
	}
	if (entry->type == LWIS_IO_ENTRY_READ_BATCH) {
		return lwis_spi_read_batch(spi_dev, entry->rw_batch.offset, entry->rw_batch.buf,
					   entry->rw_batch.size_in_bytes);
	}
	if (entry->type == LWIS_IO_ENTRY_WRITE_BATCH) {
		return lwis_spi_write_batch(spi_dev, entry->rw_batch.offset, entry->rw_batch.buf,
					    entry->rw_batch.size_in_bytes);
	}
	dev_err(spi_dev->base_dev.dev, "Invalid IO entry type: %d\n", entry->type);
	return -EINVAL;
}
