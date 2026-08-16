/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>

#include "model_store.h"

LOG_MODULE_REGISTER(model_store, LOG_LEVEL_INF);

BUILD_ASSERT(sizeof(struct iotv_hdr) == IOTV_HDR_LEN, "IOTV header is 32 B");

#define MODEL_PARTITION slot1_partition
#define FLASH_SECTOR    4096u

const char *iotv_validate(const uint8_t *buf, size_t len)
{
	const struct iotv_hdr *h = (const struct iotv_hdr *)buf;

	if (len < IOTV_HDR_LEN) {
		return "blob shorter than header";
	}
	if (memcmp(h->magic, IOTV_MAGIC, 4) != 0) {
		return "bad magic (want IOTV)";
	}
	if (h->fmt_ver != IOTV_FMT_VER) {
		return "unsupported IOTV format version";
	}
	if (h->model_len == 0 || h->model_len > MODEL_MAX_BLOB - IOTV_HDR_LEN) {
		return "model length out of range";
	}
	if (len != IOTV_HDR_LEN + h->model_len) {
		return "length does not match header";
	}
	if (crc32_ieee(buf + IOTV_HDR_LEN, h->model_len) != h->crc) {
		return "payload CRC mismatch";
	}
	return NULL;
}

void iotv_wrap_in_place(uint8_t *buf, size_t payload_len, uint16_t model_ver,
			const char *name)
{
	struct iotv_hdr *h = (struct iotv_hdr *)buf;

	memcpy(h->magic, IOTV_MAGIC, 4);
	h->fmt_ver = IOTV_FMT_VER;
	h->model_ver = model_ver;
	h->model_len = (uint32_t)payload_len;
	h->crc = crc32_ieee(buf + IOTV_HDR_LEN, payload_len);
	memset(h->name, 0, sizeof(h->name));
	strncpy(h->name, name, sizeof(h->name) - 1);
}

int model_store_save(const uint8_t *iotv_blob, size_t len)
{
	const struct flash_area *fa;
	size_t erase_len = ROUND_UP(len, FLASH_SECTOR);
	int ret;

	ret = flash_area_open(PARTITION_ID(MODEL_PARTITION), &fa);
	if (ret != 0) {
		return ret;
	}
	if (erase_len > fa->fa_size) {
		flash_area_close(fa);
		return -EFBIG;
	}

	LOG_INF("persisting model (%u B; erasing %u B first)...",
		(unsigned int)len, (unsigned int)erase_len);
	ret = flash_area_erase(fa, 0, erase_len);
	if (ret == 0) {
		ret = flash_area_write(fa, 0, iotv_blob, len);
	}
	flash_area_close(fa);
	if (ret != 0) {
		LOG_ERR("model persist failed (%d)", ret);
	} else {
		LOG_INF("model persisted to " STRINGIFY(MODEL_PARTITION));
	}
	return ret;
}

int model_store_load(uint8_t *buf, size_t buf_size, size_t *out_len)
{
	const struct flash_area *fa;
	struct iotv_hdr hdr;
	const char *err;
	size_t total;
	int ret;

	ret = flash_area_open(PARTITION_ID(MODEL_PARTITION), &fa);
	if (ret != 0) {
		return ret;
	}

	ret = flash_area_read(fa, 0, &hdr, sizeof(hdr));
	if (ret != 0) {
		goto out;
	}
	if (memcmp(hdr.magic, IOTV_MAGIC, 4) != 0) {
		ret = -ENOENT; /* empty/erased store: not an error */
		goto out;
	}
	if (hdr.fmt_ver != IOTV_FMT_VER ||
	    hdr.model_len == 0 || hdr.model_len > MODEL_MAX_BLOB - IOTV_HDR_LEN) {
		LOG_WRN("stored model header invalid; ignoring");
		ret = -ENOENT;
		goto out;
	}
	total = IOTV_HDR_LEN + hdr.model_len;
	if (total > buf_size) {
		ret = -ENOMEM;
		goto out;
	}

	ret = flash_area_read(fa, 0, buf, total);
	if (ret != 0) {
		goto out;
	}
	err = iotv_validate(buf, total);
	if (err != NULL) {
		LOG_WRN("stored model rejected: %s", err);
		ret = -ENOENT;
		goto out;
	}
	*out_len = total;

out:
	flash_area_close(fa);
	return ret;
}

int model_store_erase(void)
{
	const struct flash_area *fa;
	int ret;

	ret = flash_area_open(PARTITION_ID(MODEL_PARTITION), &fa);
	if (ret != 0) {
		return ret;
	}
	ret = flash_area_erase(fa, 0, FLASH_SECTOR);
	flash_area_close(fa);
	return ret;
}
