/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 */
#include <errno.h>
#include <string.h>

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>

#include "png_gray.h"

/* Deflate "stored" blocks are capped at 65535 bytes each. */
#define STORED_BLOCK_MAX 65535U

struct png_writer {
	uint8_t *buf;
	size_t cap;
	size_t len;
	int err;
};

static void put_bytes(struct png_writer *w, const void *data, size_t n)
{
	if (w->err != 0) {
		return;
	}
	if (w->len + n > w->cap) {
		w->err = -ENOMEM;
		return;
	}
	memcpy(w->buf + w->len, data, n);
	w->len += n;
}

static void put_be32(struct png_writer *w, uint32_t v)
{
	uint8_t b[4];

	sys_put_be32(v, b);
	put_bytes(w, b, 4);
}

/* Write one PNG chunk: length, tag, payload, CRC32(tag+payload). */
static void put_chunk(struct png_writer *w, const char tag[4],
		      const uint8_t *payload, size_t n)
{
	put_be32(w, (uint32_t)n);

	size_t crc_start = w->len;

	put_bytes(w, tag, 4);
	put_bytes(w, payload, n);
	if (w->err == 0) {
		put_be32(w, crc32_ieee(w->buf + crc_start, 4 + n));
	}
}

int png_gray_encode(const uint8_t *gray, uint16_t w, uint16_t h,
		    uint8_t *out, size_t out_size)
{
	static const uint8_t magic[8] = { 0x89, 'P', 'N', 'G',
					  '\r', '\n', 0x1a, '\n' };
	struct png_writer pw = { .buf = out, .cap = out_size };
	uint8_t ihdr[13];

	put_bytes(&pw, magic, sizeof(magic));

	sys_put_be32(w, &ihdr[0]);
	sys_put_be32(h, &ihdr[4]);
	ihdr[8] = 8;  /* bit depth */
	ihdr[9] = 0;  /* color type: grayscale */
	ihdr[10] = 0; /* compression */
	ihdr[11] = 0; /* filter */
	ihdr[12] = 0; /* interlace */
	put_chunk(&pw, "IHDR", ihdr, sizeof(ihdr));

	/*
	 * IDAT: a raw zlib stream of stored deflate blocks over the scanline
	 * data (each row prefixed with filter byte 0). Build the IDAT chunk
	 * in place: reserve the length/tag, stream the blocks, then patch
	 * length + CRC + append Adler-32.
	 */
	size_t raw_total = ((size_t)w + 1) * h;
	size_t idat_len_pos = pw.len;

	put_be32(&pw, 0);            /* chunk length, patched below */
	size_t idat_tag_pos = pw.len;

	put_bytes(&pw, "IDAT", 4);

	/* zlib header: 32K window, no preset dict, check bits valid. */
	static const uint8_t zhdr[2] = { 0x78, 0x01 };

	put_bytes(&pw, zhdr, sizeof(zhdr));

	uint32_t adler_a = 1, adler_b = 0;
	size_t emitted = 0;
	uint16_t row = 0;
	uint16_t col = 0; /* 0 = filter byte pending, else 1 + pixel index */

	while (emitted < raw_total) {
		size_t block = MIN(raw_total - emitted, (size_t)STORED_BLOCK_MAX);
		uint8_t bhdr[5];

		bhdr[0] = (emitted + block == raw_total) ? 1 : 0; /* BFINAL */
		sys_put_le16((uint16_t)block, &bhdr[1]);
		sys_put_le16((uint16_t)~block, &bhdr[3]);
		put_bytes(&pw, bhdr, sizeof(bhdr));

		for (size_t i = 0; i < block; i++) {
			uint8_t byte;

			if (col == 0) {
				byte = 0; /* filter: none */
				col = 1;
			} else {
				byte = gray[(size_t)row * w + (col - 1)];
				if (++col == (uint16_t)(w + 1)) {
					col = 0;
					row++;
				}
			}
			put_bytes(&pw, &byte, 1);
			/* Adler-32 (mod every step: sizes here stay small). */
			adler_a = (adler_a + byte) % 65521U;
			adler_b = (adler_b + adler_a) % 65521U;
		}
		emitted += block;
	}
	put_be32(&pw, (adler_b << 16) | adler_a);

	if (pw.err != 0) {
		return pw.err;
	}

	/* Patch IDAT length and append its CRC. */
	size_t idat_payload = pw.len - idat_tag_pos - 4;

	sys_put_be32((uint32_t)idat_payload, pw.buf + idat_len_pos);
	put_be32(&pw, crc32_ieee(pw.buf + idat_tag_pos, 4 + idat_payload));

	put_chunk(&pw, "IEND", NULL, 0);

	return (pw.err == 0) ? (int)pw.len : pw.err;
}
