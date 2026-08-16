/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Store-and-forward spool: a raw-sector ring queue on the on-SOM eMMC.
 *
 * The i.MX93 A55 target has no NOR flash and mounts no filesystem; the SDK
 * already persists the device identity to a raw eMMC sector region
 * (CONFIG_IOTCONNECT_IDENTITY_DISK). The spool follows the same pattern: a
 * fixed ring of 3-sector (1536 B) slots, each holding one ready-to-publish
 * telemetry JSON string with a magic/sequence/CRC32 header. No filesystem,
 * no format step, deterministic wear.
 *
 * While the gateway is offline every record (its own and each child's) is
 * pushed here with its original "dt" timestamp embedded in the JSON; on
 * reconnect the main loop drains oldest-first and the platform backfills the
 * timeline. When the ring fills, the OLDEST record is overwritten and
 * counted in drops (newest data wins). Popped slots are invalidated on
 * commit so a reboot never re-sends drained records.
 *
 * Location/size come from Kconfig (CONFIG_GATEWAY_SPOOL_*); the defaults sit
 * well above the identity region on the otherwise-unused eMMC user area.
 */
#ifndef SPOOL_H
#define SPOOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SPOOL_SLOT_SECTORS 3
#define SPOOL_PAYLOAD_MAX  (SPOOL_SLOT_SECTORS * 512 - 16 /* header */ - 1)

struct spool_stats {
	uint32_t count;     /* records currently queued */
	uint32_t capacity;  /* total slots */
	uint32_t pushed;    /* since boot */
	uint32_t drained;   /* since boot */
	uint32_t drops;     /* ring-full overwrites + oversize rejects */
};

/* Scan the ring to recover head/tail. Returns 0 or -errno (disk missing). */
int spool_init(void);

/* Queue one JSON record (NUL-terminated). Returns 0, or -EMSGSIZE/-EIO. */
int spool_push(const char *json);

/*
 * Read the oldest record into buf without removing it. Returns its length,
 * 0 when the spool is empty, or a negative errno.
 */
int spool_peek(char *buf, size_t buf_size);

/* Invalidate the record last returned by spool_peek() (after publishing). */
int spool_commit(void);

/* Erase every slot header (slow: one write per slot). Returns 0 or -errno. */
int spool_wipe(void);

void spool_get_stats(struct spool_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* SPOOL_H */
