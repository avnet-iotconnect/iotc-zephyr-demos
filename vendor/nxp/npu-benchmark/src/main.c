/*
 * Copyright (c) 2020-2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * IOTCONNECT NXP eIQ Neutron NPU benchmark.
 *
 * Demonstrates NPU acceleration by running the same quantized model and timing
 * each inference, then streaming the result to /IOTCONNECT as telemetry. Build
 * the default (CONFIG_MCUX_HAS_NPU=y) image to run on the Neutron NPU, and the
 * cpu.conf image to run the same model on the Cortex-M33; the dashboard charts
 * "neutron-npu" against "cpu-m33" and the gap between them is the demo.
 *
 * The model interface is the C shim in model_bench.h; connectivity reuses the
 * IOTCONNECT Zephyr SDK exactly like the telemetry sample. Set
 * CONFIG_APP_PUBLISH_TO_CLOUD=n to run the benchmark console-only (no network,
 * no credentials) for quick NPU-vs-CPU timing on the bench.
 */

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "model_bench.h"

#ifdef CONFIG_APP_PUBLISH_TO_CLOUD
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/conn_mgr_monitor.h>

#include "iotconnect.h"
#include "iotconnect_telemetry.h"
#include "iotc_time.h"

/* Real per-device credentials, generated into a git-ignored header exactly like
 * the SDK telemetry sample (see README). Defines device_cert_pem,
 * device_key_pem, broker_ca_pem, dra_ca_pem. */
#include "device_credentials.h"
#endif /* CONFIG_APP_PUBLISH_TO_CLOUD */

LOG_MODULE_REGISTER(iotc_npu_benchmark, CONFIG_APP_LOG_LEVEL);

#define APP_VERSION "1.0.0"

/* The one line that says which engine ran -- reported as a telemetry string so
 * IOTCONNECT can group/series by accelerator. */
#ifdef CONFIG_MCUX_HAS_NPU
#define ACCEL_LABEL "neutron-npu"
#else
#define ACCEL_LABEL "cpu-m33"
#endif

/* ---------------------------------------------------------------------------
 * Benchmark
 * ------------------------------------------------------------------------- */

struct bench_stats {
	uint32_t iterations;
	uint32_t min_us;
	uint32_t max_us;
	uint64_t sum_us;
	int top_index;
	unsigned arena_kb;
};

static void run_benchmark_cycle(struct bench_stats *st, unsigned seed)
{
	const int warmup = CONFIG_APP_BENCH_WARMUP;
	const int iters = CONFIG_APP_BENCH_ITERATIONS;

	st->iterations = 0;
	st->min_us = UINT32_MAX;
	st->max_us = 0;
	st->sum_us = 0;
	st->arena_kb = bench_model_arena_kb();

	/* Warm-up: first invoke pays one-time costs (NPU firmware load, caches);
	 * exclude it so the reported numbers are steady-state. */
	for (int i = 0; i < warmup; i++) {
		bench_model_fill_input(seed + (unsigned)i);
		(void)bench_model_invoke();
	}

	for (int i = 0; i < iters; i++) {
		bench_model_fill_input(seed + (unsigned)(warmup + i));

		uint32_t t0 = k_cycle_get_32();
		int rc = bench_model_invoke();
		uint32_t t1 = k_cycle_get_32();

		if (rc) {
			LOG_ERR("Inference failed at iteration %d", i);
			continue;
		}

		/* Unsigned subtraction is wrap-safe for a single inference. */
		uint32_t us = k_cyc_to_us_floor32(t1 - t0);

		st->sum_us += us;
		st->iterations++;
		if (us < st->min_us) {
			st->min_us = us;
		}
		if (us > st->max_us) {
			st->max_us = us;
		}
	}

	if (st->iterations == 0) {
		st->min_us = 0;
	}

	st->top_index = -1;
	(void)bench_model_top1(&st->top_index);
}

static uint32_t avg_us(const struct bench_stats *st)
{
	return st->iterations ? (uint32_t)(st->sum_us / st->iterations) : 0;
}

static void log_stats(const struct bench_stats *st)
{
	uint32_t a = avg_us(st);

	LOG_INF("%s | %s | avg %u.%03u ms (min %u.%03u / max %u.%03u) | "
		"%u inf/s | top=%d | arena %u KB",
		ACCEL_LABEL, bench_model_name(),
		a / 1000U, a % 1000U,
		st->min_us / 1000U, st->min_us % 1000U,
		st->max_us / 1000U, st->max_us % 1000U,
		a ? (1000000U / a) : 0U,
		st->top_index, st->arena_kb);
}

/* ---------------------------------------------------------------------------
 * Cloud path (compiled out when CONFIG_APP_PUBLISH_TO_CLOUD=n)
 * ------------------------------------------------------------------------- */
#ifdef CONFIG_APP_PUBLISH_TO_CLOUD

static K_SEM_DEFINE(l4_connected_sem, 0, 1);
static struct net_mgmt_event_callback l4_cb;

#define L4_EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

static void l4_event_handler(struct net_mgmt_event_callback *cb,
			     uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);

	switch (mgmt_event) {
	case NET_EVENT_L4_CONNECTED:
		LOG_INF("Network connectivity established (L4 up)");
		k_sem_give(&l4_connected_sem);
		break;
	case NET_EVENT_L4_DISCONNECTED:
		LOG_WRN("Network connectivity lost (L4 down)");
		break;
	default:
		break;
	}
}

static int network_up(void)
{
	struct net_if *iface = net_if_get_default();

	if (!iface) {
		LOG_ERR("No network interface available");
		return -ENODEV;
	}

	net_mgmt_init_event_callback(&l4_cb, l4_event_handler, L4_EVENT_MASK);
	net_mgmt_add_event_callback(&l4_cb);

	if (!net_if_is_up(iface)) {
		int ret = net_if_up(iface);

		if (ret && ret != -EALREADY) {
			LOG_ERR("net_if_up failed (%d)", ret);
			return ret;
		}
	}

	conn_mgr_mon_resend_status();

	LOG_INF("Waiting for network connectivity...");
	k_sem_take(&l4_connected_sem, K_FOREVER);

	return 0;
}

static void on_connection_status(IotConnectMqttStatus status)
{
	switch (status) {
	case IOTC_CS_MQTT_CONNECTED:
		LOG_INF("IOTCONNECT: MQTT connected");
		break;
	case IOTC_CS_MQTT_DISCONNECTED:
		LOG_WRN("IOTCONNECT: MQTT disconnected");
		break;
	default:
		break;
	}
}

static void on_command(IotclC2dEventData data)
{
	const char *cmd = iotcl_c2d_get_command(data);
	const char *ack = iotcl_c2d_get_ack_id(data);

	LOG_INF("C2D command: %s", cmd ? cmd : "(null)");

	if (ack) {
		(void)iotcl_mqtt_send_cmd_ack(ack,
					      IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK,
					      NULL);
	}
}

static void on_ota(IotclC2dEventData data)
{
	const char *ack = iotcl_c2d_get_ack_id(data);

	if (ack) {
		(void)iotcl_mqtt_send_ota_ack(ack,
					      IOTCL_C2D_EVT_OTA_DOWNLOAD_DONE,
					      NULL);
	}
}

static int publish_benchmark(const struct bench_stats *st)
{
	IotclMessageHandle msg = iotc_telemetry_begin();
	uint32_t a = avg_us(st);

	if (!msg) {
		LOG_ERR("Failed to allocate telemetry message");
		return -ENOMEM;
	}

	iotcl_telemetry_set_string(msg, "accelerator", ACCEL_LABEL);
	iotcl_telemetry_set_string(msg, "model", bench_model_name());
	iotcl_telemetry_set_number(msg, "inf_ms_avg", a / 1000.0);
	iotcl_telemetry_set_number(msg, "inf_ms_min", st->min_us / 1000.0);
	iotcl_telemetry_set_number(msg, "inf_ms_max", st->max_us / 1000.0);
	iotcl_telemetry_set_number(msg, "inf_per_sec", a ? (1000000.0 / a) : 0.0);
	iotcl_telemetry_set_number(msg, "iterations", (double)st->iterations);
	iotcl_telemetry_set_number(msg, "top_index", (double)st->top_index);
	iotcl_telemetry_set_number(msg, "arena_kb", (double)st->arena_kb);
	iotcl_telemetry_set_string(msg, "version", APP_VERSION);

	return iotc_telemetry_send(msg, false);
}

static int cloud_run(void)
{
	int ret = network_up();

	if (ret) {
		LOG_ERR("Network bring-up failed (%d)", ret);
		return ret;
	}

	ret = iotc_time_sync(CONFIG_IOTCONNECT_SNTP_SERVER,
			     CONFIG_IOTCONNECT_SNTP_TIMEOUT_MS);
	if (ret) {
		LOG_ERR("SNTP time sync failed (%d); TLS will likely fail", ret);
		return ret;
	}

	IotConnectClientConfig config;

	iotconnect_sdk_init_config(&config);

#if defined(CONFIG_IOTCONNECT_CT_AWS)
	config.connection_type = IOTC_CT_AWS;
#elif defined(CONFIG_IOTCONNECT_CT_AZURE)
	config.connection_type = IOTC_CT_AZURE;
#else
	config.connection_type = IOTC_CT_UNDEFINED;
#endif

	config.cpid = NULL;
	config.env = NULL;
	config.duid = NULL;

	config.auth_info.type = IOTC_AT_X509;
	config.auth_info.ca_cert = broker_ca_pem;
	config.auth_info.ca_cert_len = sizeof(broker_ca_pem);
	config.auth_info.dra_ca = dra_ca_pem;
	config.auth_info.dra_ca_len = sizeof(dra_ca_pem);
	config.auth_info.data.cert_info.device_cert = device_cert_pem;
	config.auth_info.data.cert_info.device_cert_len = sizeof(device_cert_pem);
	config.auth_info.data.cert_info.device_key = device_key_pem;
	config.auth_info.data.cert_info.device_key_len = sizeof(device_key_pem);

	config.status_cb = on_connection_status;
	config.cmd_cb = on_command;
	config.ota_cb = on_ota;
	config.verbose = true;

	ret = iotconnect_sdk_init(&config);
	if (ret) {
		LOG_ERR("iotconnect_sdk_init failed (%d)", ret);
		return ret;
	}

	unsigned seed = 0;

	while (true) {
		ret = iotconnect_sdk_connect();
		if (ret) {
			LOG_ERR("connect failed (%d); retrying", ret);
			k_sleep(K_SECONDS(5));
			continue;
		}

		while (iotconnect_sdk_is_connected()) {
			struct bench_stats st;

			run_benchmark_cycle(&st, seed++);
			log_stats(&st);
			(void)publish_benchmark(&st);

			k_sleep(K_SECONDS(CONFIG_APP_BENCH_INTERVAL_SEC));
		}

		iotconnect_sdk_disconnect();
		LOG_WRN("Disconnected; reconnecting...");
		k_sleep(K_SECONDS(5));
	}

	iotconnect_sdk_deinit();
	return 0;
}

#endif /* CONFIG_APP_PUBLISH_TO_CLOUD */

/* ---------------------------------------------------------------------------
 * main
 * ------------------------------------------------------------------------- */

int main(void)
{
	LOG_INF("IOTCONNECT NPU benchmark starting (app v%s, accelerator=%s)",
		APP_VERSION, ACCEL_LABEL);

	if (bench_model_init() != 0) {
		LOG_ERR("Model init failed");
		return -EIO;
	}
	LOG_INF("Model '%s' ready (%u KB arena used)",
		bench_model_name(), bench_model_arena_kb());

#ifdef CONFIG_APP_PUBLISH_TO_CLOUD
	return cloud_run();
#else
	LOG_INF("Cloud publishing disabled; local benchmark loop only.");

	unsigned seed = 0;

	while (true) {
		struct bench_stats st;

		run_benchmark_cycle(&st, seed++);
		log_stats(&st);
		k_sleep(K_SECONDS(CONFIG_APP_BENCH_INTERVAL_SEC));
	}

	return 0;
#endif /* CONFIG_APP_PUBLISH_TO_CLOUD */
}
