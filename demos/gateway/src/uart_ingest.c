/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>

#include "uart_ingest.h"

LOG_MODULE_REGISTER(uart_ingest, LOG_LEVEL_INF);

#define INGEST_PREFIX "IOTC-TELEMETRY: "
#define INGEST_PREFIX_LEN (sizeof(INGEST_PREFIX) - 1)

/* Per-link queue of complete lines: [u16 len][len bytes] records. Sized to
 * absorb ~10 s of a chatty source while the main loop is busy reconnecting. */
#define LINE_RING_SIZE (16 * 1024)

struct link {
	const struct device *dev;
	/* ISR line assembly */
	char line[INGEST_LINE_MAX];
	size_t line_len;
	bool overlong;
	/* complete-line queue, ISR producer / thread consumer */
	struct ring_buf rb;
	uint8_t rb_storage[LINE_RING_SIZE];
	struct k_spinlock lock;
	struct ingest_stats stats;
};

#if DT_NODE_EXISTS(DT_ALIAS(iotc_link0))
#define LINK0_DEV DEVICE_DT_GET(DT_ALIAS(iotc_link0))
#else
#define LINK0_DEV NULL
#endif
#if DT_NODE_EXISTS(DT_ALIAS(iotc_link1))
#define LINK1_DEV DEVICE_DT_GET(DT_ALIAS(iotc_link1))
#else
#define LINK1_DEV NULL
#endif

static struct link links[INGEST_MAX_LINKS] = {
	{ .dev = LINK0_DEV },
	{ .dev = LINK1_DEV },
};

int uart_ingest_link_count(void)
{
	int n = 0;

	for (int i = 0; i < INGEST_MAX_LINKS; i++) {
		if (links[i].dev != NULL) {
			n++;
		}
	}
	return n;
}

const char *uart_ingest_link_name(int link)
{
	if (link < 0 || link >= INGEST_MAX_LINKS || links[link].dev == NULL) {
		return "none";
	}
	return links[link].dev->name;
}

/* A full line has been assembled in ISR context; queue it if it is a
 * telemetry line, count it against the stats either way. */
static void line_complete(struct link *l)
{
	if (l->overlong) {
		l->overlong = false;
		l->line_len = 0;
		l->stats.lines_dropped++;
		return;
	}
	l->line[l->line_len] = '\0';

	if (l->line_len <= INGEST_PREFIX_LEN ||
	    strncmp(l->line, INGEST_PREFIX, INGEST_PREFIX_LEN) != 0) {
		/* Boot banner / log noise from the source: ignore silently. */
		l->line_len = 0;
		return;
	}

	const char *json = l->line + INGEST_PREFIX_LEN;
	uint16_t len = (uint16_t)(l->line_len - INGEST_PREFIX_LEN);

	K_SPINLOCK(&l->lock) {
		if (ring_buf_space_get(&l->rb) < sizeof(len) + len) {
			l->stats.lines_dropped++;
		} else {
			ring_buf_put(&l->rb, (uint8_t *)&len, sizeof(len));
			ring_buf_put(&l->rb, (const uint8_t *)json, len);
			l->stats.lines_ok++;
			l->stats.last_seen_ms = k_uptime_get();
		}
	}
	l->line_len = 0;
}

static void uart_isr(const struct device *dev, void *user_data)
{
	struct link *l = user_data;
	uint8_t chunk[64];

	if (!uart_irq_update(dev)) {
		return;
	}
	while (uart_irq_rx_ready(dev)) {
		int n = uart_fifo_read(dev, chunk, sizeof(chunk));

		for (int i = 0; i < n; i++) {
			char c = (char)chunk[i];

			if (c == '\n' || c == '\r') {
				if (l->line_len > 0 || l->overlong) {
					line_complete(l);
				}
			} else if (l->line_len < INGEST_LINE_MAX - 1) {
				l->line[l->line_len++] = c;
			} else {
				l->overlong = true;
			}
		}
	}
}

int uart_ingest_init(void)
{
	for (int i = 0; i < INGEST_MAX_LINKS; i++) {
		struct link *l = &links[i];

		if (l->dev == NULL) {
			continue;
		}
		if (!device_is_ready(l->dev)) {
			LOG_ERR("link%d UART %s not ready", i, l->dev->name);
			return -ENODEV;
		}
		ring_buf_init(&l->rb, sizeof(l->rb_storage), l->rb_storage);
		uart_irq_callback_user_data_set(l->dev, uart_isr, l);
		uart_irq_rx_enable(l->dev);
		LOG_INF("link%d: listening on %s", i, l->dev->name);
	}
	return 0;
}

int uart_ingest_pop(int link, char *buf, size_t buf_size)
{
	struct link *l;
	uint16_t len = 0;
	int out = 0;

	if (link < 0 || link >= INGEST_MAX_LINKS || links[link].dev == NULL) {
		return -EINVAL;
	}
	l = &links[link];

	K_SPINLOCK(&l->lock) {
		if (ring_buf_size_get(&l->rb) < sizeof(len)) {
			out = 0;
		} else {
			ring_buf_get(&l->rb, (uint8_t *)&len, sizeof(len));
			if (len >= buf_size) {
				/* Cannot deliver; discard to stay in sync. */
				uint8_t sink[64];

				for (uint16_t r = len; r > 0;) {
					uint32_t g = ring_buf_get(&l->rb, sink,
								  MIN(r, sizeof(sink)));
					r -= (uint16_t)g;
				}
				l->stats.lines_dropped++;
				out = -EMSGSIZE;
			} else {
				ring_buf_get(&l->rb, (uint8_t *)buf, len);
				buf[len] = '\0';
				out = len;
			}
		}
	}
	return out;
}

void uart_ingest_get_stats(int link, struct ingest_stats *out)
{
	memset(out, 0, sizeof(*out));
	if (link >= 0 && link < INGEST_MAX_LINKS && links[link].dev != NULL) {
		K_SPINLOCK(&links[link].lock) {
			*out = links[link].stats;
		}
	}
}
