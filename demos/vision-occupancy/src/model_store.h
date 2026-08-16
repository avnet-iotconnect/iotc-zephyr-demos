/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * IOTV model envelope + on-flash persistence for cloud-pushed vision models.
 *
 * A vision model travels as an "IOTV" blob: a 32-byte header (magic, format
 * version, model version, display name, payload length, CRC32) followed by
 * the raw .tflite flatbuffer. tools/pack_model.py builds one. The envelope
 * carries the same validation discipline as the ml-model-update demo's IOTM
 * format, scaled up from 124-byte MLPs to ~300 KB CNNs.
 *
 * Persistence: NVS/settings items are capped well below 300 KB, so pushed
 * models are stored raw in the slot1_partition (7 MB, unused by these
 * no-MCUboot demos) via the flash-area API. If this demo is ever combined
 * with MCUboot dual-slot OTA, move the store to a dedicated partition.
 */
#ifndef MODEL_STORE_H
#define MODEL_STORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IOTV_MAGIC   "IOTV"
#define IOTV_FMT_VER 1
#define IOTV_HDR_LEN 32

/* Staging/store ceiling for one enveloped model (header + .tflite). */
#define MODEL_MAX_BLOB (512 * 1024)

struct iotv_hdr {
	char magic[4];       /* "IOTV" */
	uint16_t fmt_ver;    /* IOTV_FMT_VER */
	uint16_t model_ver;  /* monotonic, chosen at pack time */
	uint32_t model_len;  /* payload (.tflite) length in bytes */
	uint32_t crc;        /* IEEE CRC32 over the payload */
	char name[16];       /* NUL-padded display name */
} __packed;

/* NULL when buf holds a well-formed IOTV blob, else a short reason. */
const char *iotv_validate(const uint8_t *buf, size_t len);

/*
 * Wrap a raw .tflite (already sitting at buf + IOTV_HDR_LEN, payload_len
 * bytes) with an IOTV header in place. Used to normalize raw-flatbuffer
 * pushes so everything downstream handles one format.
 */
void iotv_wrap_in_place(uint8_t *buf, size_t payload_len, uint16_t model_ver,
			const char *name);

/* Persist a validated IOTV blob to the model partition. Returns 0 or -errno.
 * Erases the required range first; several seconds for a ~300 KB model. */
int model_store_save(const uint8_t *iotv_blob, size_t len);

/*
 * Load the persisted IOTV blob into buf (buf_size >= MODEL_MAX_BLOB).
 * Returns 0 and sets *out_len when a valid blob was restored; -ENOENT when
 * the store is empty/invalid; other -errno on flash errors.
 */
int model_store_load(uint8_t *buf, size_t buf_size, size_t *out_len);

/* Invalidate the store (erase the header sector). Returns 0 or -errno. */
int model_store_erase(void);

#ifdef __cplusplus
}
#endif

#endif /* MODEL_STORE_H */
