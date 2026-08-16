/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Minimal 8-bit grayscale PNG encoder using stored (uncompressed) deflate
 * blocks -- no zlib dependency, just CRC32 (zephyr sys/crc) and an inline
 * Adler-32. Output size ~= w*h + h + 90 bytes.
 */
#ifndef PNG_GRAY_H
#define PNG_GRAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Worst-case encoded size for a w x h frame (header + stored blocks). */
#define PNG_GRAY_MAX_SIZE(w, h) ((size_t)(w) * (h) + (h) + 128)

/*
 * Encode gray (w*h bytes, row-major) into out. Returns the PNG byte count,
 * or a negative errno (-ENOMEM if out_size is too small).
 */
int png_gray_encode(const uint8_t *gray, uint16_t w, uint16_t h,
		    uint8_t *out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* PNG_GRAY_H */
