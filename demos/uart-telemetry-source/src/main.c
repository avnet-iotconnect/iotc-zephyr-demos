/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * IOTCONNECT UART telemetry source -- for connectivity-less MCUs.
 *
 * Proves the portable Avnet /IOTCONNECT protocol core (iotc-c-lib) runs on
 * silicon whose Zephyr port has NO IP driver (no Ethernet/Wi-Fi, so no MQTT/TLS):
 * every few seconds it builds an IOTCONNECT 2.1 telemetry message with the
 * iotcl_telemetry_* API, serializes it to JSON, and prints it on the console
 * UART. The emitted "IOTC-TELEMETRY: {...}" line is meant to be relayed by a
 * gateway (over CAN FD / UART, or a future cell-modem Click bearer) to an
 * IP-capable node running the full iotc-zephyr-sdk.
 *
 * This is the "telemetry source" pattern that lets a connectivity-less
 * safety/industrial or wireless-only MCU contribute to IOTCONNECT today.
 * The board name is taken from CONFIG_BOARD, so one app serves every target;
 * add a board by dropping a boards/<board>.conf in this demo.
 *
 * Verified on FRDM-MCXE31B (Cortex-M7) and FRDM-MCXW72 (Cortex-M33, 802.15.4).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "iotcl.h"
#include "iotcl_telemetry.h"

#define TELEMETRY_PERIOD_S 5

static int iotc_setup(void)
{
	/* Route iotc-c-lib (and cJSON) allocations onto the Zephyr heap. MUST be
	 * called before any other library call. */
	iotcl_configure_dynamic_memory(k_malloc, k_free);

	IotclClientConfig cfg;

	iotcl_init_client_config(&cfg);
	cfg.device.instance_type = IOTCL_DCT_CUSTOM; /* building JSON, not connecting */
	cfg.device.cpid = "DEMO-CPID";
	cfg.device.duid = CONFIG_BOARD "-01";
	cfg.mqtt_send_cb = NULL; /* serialize directly instead of publishing */
	cfg.time_fn = NULL;      /* let the server timestamp on arrival */

	return iotcl_init(&cfg);
}

int main(void)
{
	printk("\n=== IOTCONNECT UART telemetry source -- %s ===\n", CONFIG_BOARD);
	printk("Building IOTCONNECT 2.1 telemetry JSON on-device via iotc-c-lib.\n");
	printk("No IP transport in this build -> output to UART for a gateway to "
	       "forward.\n\n");

	int ret = iotc_setup();

	if (ret != 0) {
		printk("iotcl_init failed: %d\n", ret);
		return 0;
	}

	uint32_t seq = 0;

	while (1) {
		IotclMessageHandle msg = iotcl_telemetry_create();

		if (msg == NULL) {
			printk("telemetry_create failed (out of memory)\n");
			k_sleep(K_SECONDS(TELEMETRY_PERIOD_S));
			continue;
		}

		/* A representative payload for a constrained edge node. */
		int64_t uptime_ms = k_uptime_get();
		double  pseudo_temp = 30.0 + (double)((uptime_ms / 1000) % 100) / 10.0;

		iotcl_telemetry_set_number(msg, "sequence", (double)seq++);
		iotcl_telemetry_set_number(msg, "uptime_s", (double)uptime_ms / 1000.0);
		iotcl_telemetry_set_number(msg, "cpu_temp_c", pseudo_temp);
		iotcl_telemetry_set_string(msg, "board", CONFIG_BOARD);
		iotcl_telemetry_set_string(msg, "bearer", "uart-source");
		iotcl_telemetry_set_string(msg, "status", "Ready");
		iotcl_telemetry_set_bool(msg, "safe_state", true);

		char *json = iotcl_telemetry_create_serialized_string(msg, false);

		if (json != NULL) {
			printk("IOTC-TELEMETRY: %s\n", json);
			iotcl_telemetry_destroy_serialized_string(json);
		} else {
			printk("telemetry serialize failed\n");
		}

		iotcl_telemetry_destroy(msg);
		k_sleep(K_SECONDS(TELEMETRY_PERIOD_S));
	}

	return 0;
}
