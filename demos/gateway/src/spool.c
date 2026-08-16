/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/crc.h>

#include "spool.h"

LOG_MODULE_REGISTER(spool, LOG_LEVEL_INF);

#define SPOOL_DISK    CONFIG_GATEWAY_SPOOL_DISK_NAME
#define BASE_SECTOR   CONFIG_GATEWAY_SPOOL_SECTOR
#define NSLOTS        CONFIG_GATEWAY_SPOOL_SLOTS
#define SLOT_BYTES    (SPOOL_SLOT_SECTORS * 512)

#define REC_MAGIC 0x52544F49u /* "IOTR" little-endian */

struct rec_hdr {
	uint32_t magic;
	uint32_t seq;
	uint16_t len;
	uint16_t rsvd;
	uint32_t crc;      /* CRC32 over the payload bytes */
} __packed;

BUILD_ASSERT(sizeof(struct rec_hdr) == 16, "spool record header is 16 B");

static K_MUTEX_DEFINE(spool_mutex);
static uint8_t slot_buf[SLOT_BYTES];  /* shared read/write staging */
static uint32_t head_seq;             /* oldest queued record */
static uint32_t tail_seq;             /* next sequence to write */
static uint32_t rec_count;
static struct spool_stats stats;
static bool ready;

static uint32_t slot_of(uint32_t seq)
{
	return BASE_SECTOR + (seq % NSLOTS) * SPOOL_SLOT_SECTORS;
}

static int read_hdr(uint32_t slot_sector, struct rec_hdr *h)
{
	/* Header lives in the slot's first sector. */
	int ret = disk_access_read(SPOOL_DISK, slot_buf, slot_sector, 1);

	if (ret == 0) {
		memcpy(h, slot_buf, sizeof(*h));
	}
	return ret;
}

int spool_init(void)
{
	uint32_t max_seq = 0, min_seq = UINT32_MAX;
	uint32_t valid = 0;
	int ret;

	ret = disk_access_init(SPOOL_DISK);
	if (ret != 0) {
		LOG_ERR("disk %s init failed (%d); spooling disabled",
			SPOOL_DISK, ret);
		return ret;
	}

	/* Recover the ring: valid slots carry the magic; the live window is
	 * the contiguous sequence range still resident in the ring. */
	for (uint32_t i = 0; i < NSLOTS; i++) {
		struct rec_hdr h;

		if (read_hdr(BASE_SECTOR + i * SPOOL_SLOT_SECTORS, &h) != 0) {
			return -EIO;
		}
		if (h.magic != REC_MAGIC || h.len > SPOOL_PAYLOAD_MAX) {
			continue;
		}
		valid++;
		max_seq = MAX(max_seq, h.seq);
		min_seq = MIN(min_seq, h.seq);
	}

	if (valid == 0) {
		head_seq = tail_seq = 0;
		rec_count = 0;
	} else {
		/* Stale slots from older overwritten laps sit below the live
		 * window; clamp to the newest NSLOTS-sized window. */
		tail_seq = max_seq + 1;
		head_seq = MAX(min_seq, (max_seq >= NSLOTS - 1) ?
					 max_seq - (NSLOTS - 1) : 0);
		rec_count = tail_seq - head_seq;
	}
	stats.capacity = NSLOTS;
	stats.count = rec_count;
	ready = true;
	LOG_INF("spool: %u slots @ sector %u, %u queued (seq %u..%u)",
		(unsigned int)NSLOTS, (unsigned int)BASE_SECTOR,
		(unsigned int)rec_count, (unsigned int)head_seq,
		(unsigned int)tail_seq);
	return 0;
}

int spool_push(const char *json)
{
	size_t len = strlen(json);
	struct rec_hdr h;
	int ret;

	if (!ready) {
		return -ENODEV;
	}
	if (len > SPOOL_PAYLOAD_MAX) {
		k_mutex_lock(&spool_mutex, K_FOREVER);
		stats.drops++;
		k_mutex_unlock(&spool_mutex);
		return -EMSGSIZE;
	}

	k_mutex_lock(&spool_mutex, K_FOREVER);
	if (rec_count == NSLOTS) {
		/* Ring full: sacrifice the oldest record, keep the newest. */
		head_seq++;
		rec_count--;
		stats.drops++;
	}

	h.magic = REC_MAGIC;
	h.seq = tail_seq;
	h.len = (uint16_t)len;
	h.rsvd = 0;
	h.crc = crc32_ieee((const uint8_t *)json, len);

	memset(slot_buf, 0, SLOT_BYTES);
	memcpy(slot_buf, &h, sizeof(h));
	memcpy(slot_buf + sizeof(h), json, len);

	ret = disk_access_write(SPOOL_DISK, slot_buf, slot_of(tail_seq),
				SPOOL_SLOT_SECTORS);
	if (ret == 0) {
		/* Flush the eMMC write cache: spooled records exist precisely
		 * to survive power loss. */
		(void)disk_access_ioctl(SPOOL_DISK, DISK_IOCTL_CTRL_SYNC, NULL);
		tail_seq++;
		rec_count++;
		stats.pushed++;
		stats.count = rec_count;
	}
	k_mutex_unlock(&spool_mutex);
	return ret;
}

int spool_peek(char *buf, size_t buf_size)
{
	struct rec_hdr h;
	int ret = 0;

	if (!ready) {
		return -ENODEV;
	}
	k_mutex_lock(&spool_mutex, K_FOREVER);
	while (rec_count > 0) {
		ret = disk_access_read(SPOOL_DISK, slot_buf, slot_of(head_seq),
				       SPOOL_SLOT_SECTORS);
		if (ret != 0) {
			break;
		}
		memcpy(&h, slot_buf, sizeof(h));
		if (h.magic != REC_MAGIC || h.seq != head_seq ||
		    h.len > SPOOL_PAYLOAD_MAX || h.len >= buf_size ||
		    crc32_ieee(slot_buf + sizeof(h), h.len) != h.crc) {
			/* Corrupt/foreign slot: skip it rather than wedge. */
			LOG_WRN("spool: skipping bad record seq %u",
				(unsigned int)head_seq);
			head_seq++;
			rec_count--;
			stats.drops++;
			stats.count = rec_count;
			continue;
		}
		memcpy(buf, slot_buf + sizeof(h), h.len);
		buf[h.len] = '\0';
		ret = h.len;
		break;
	}
	k_mutex_unlock(&spool_mutex);
	return (rec_count == 0 && ret == 0) ? 0 : ret;
}

int spool_commit(void)
{
	int ret = 0;

	if (!ready) {
		return -ENODEV;
	}
	k_mutex_lock(&spool_mutex, K_FOREVER);
	if (rec_count > 0) {
		/* Invalidate the slot header so a reboot cannot re-send it. */
		memset(slot_buf, 0, 512);
		ret = disk_access_write(SPOOL_DISK, slot_buf,
					slot_of(head_seq), 1);
		head_seq++;
		rec_count--;
		stats.drained++;
		stats.count = rec_count;
	}
	k_mutex_unlock(&spool_mutex);
	return ret;
}

int spool_wipe(void)
{
	int ret = 0;

	if (!ready) {
		return -ENODEV;
	}
	k_mutex_lock(&spool_mutex, K_FOREVER);
	memset(slot_buf, 0, 512);
	for (uint32_t i = 0; i < NSLOTS && ret == 0; i++) {
		ret = disk_access_write(SPOOL_DISK, slot_buf,
					BASE_SECTOR + i * SPOOL_SLOT_SECTORS, 1);
	}
	head_seq = tail_seq = 0;
	rec_count = 0;
	stats.count = 0;
	k_mutex_unlock(&spool_mutex);
	return ret;
}

void spool_get_stats(struct spool_stats *out)
{
	k_mutex_lock(&spool_mutex, K_FOREVER);
	*out = stats;
	k_mutex_unlock(&spool_mutex);
}
