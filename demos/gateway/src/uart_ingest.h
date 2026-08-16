/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * UART ingest for the gateway demo: receives line-framed IOTCONNECT 2.1
 * telemetry from connectivity-less MCUs running the uart-telemetry-source
 * demo, which print one message per period as:
 *
 *   IOTC-TELEMETRY: {"d":[{"d":{...fields...}}]}\n
 *
 * Each link is a UART named by a devicetree alias (iotc-link0, iotc-link1).
 * RX runs interrupt-driven; complete "IOTC-TELEMETRY:" lines are queued in a
 * per-link ring buffer (anything else on the wire -- boot banners, logs -- is
 * discarded). The main loop pops lines at its own pace; if it stalls (e.g.
 * during a reconnect attempt) the ring absorbs the burst and the drop
 * counters record any overflow honestly.
 */
#ifndef UART_INGEST_H
#define UART_INGEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longest accepted JSON line (source messages incl. vitals are < 1 KB). */
#define INGEST_LINE_MAX 1024

#define INGEST_MAX_LINKS 2

struct ingest_stats {
	uint32_t lines_ok;      /* complete IOTC-TELEMETRY lines queued */
	uint32_t lines_dropped; /* ring full or line overlong */
	int64_t last_seen_ms;   /* k_uptime of the last good line; 0 = never */
};

/* Number of links present in the devicetree (0..INGEST_MAX_LINKS). */
int uart_ingest_link_count(void);

/* Human-readable device name of a link's UART (for logs/ACKs). */
const char *uart_ingest_link_name(int link);

/* Start RX on all links. Returns 0, or -ENODEV if a wired link is not ready. */
int uart_ingest_init(void);

/*
 * Pop the oldest queued JSON line from a link into buf (NUL-terminated,
 * "IOTC-TELEMETRY: " prefix already stripped). Returns the line length,
 * 0 when the queue is empty, or a negative errno.
 */
int uart_ingest_pop(int link, char *buf, size_t buf_size);

void uart_ingest_get_stats(int link, struct ingest_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* UART_INGEST_H */
