/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * IOTCONNECT thread for the face-detect demo.
 *
 * Replaces the original vendored iotc.c / iotc-c-lib glue with the
 * iotc-zephyr-sdk. Connectivity is the LTE IoT 12 Click (generic Zephyr
 * cellular modem driver + PPP); identity uses the quickstart provisioning
 * flow (on-device key + NVS identity, public CA roots compiled in).
 *
 * Consumes inference results from the FIFO fed by main() and publishes the
 * detection count per the mcxn947-facedet device template (faces,
 * local_timestamp), rate-limited by CONFIG_IOTC_MQTT_DEVICE_REPORT_SEC.
 */

#include <time.h>
#include <stdbool.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/pm/device.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>

#include "iotconnect.h"
#include "iotcl.h"
#include "iotcl_telemetry.h"
#include "iotconnect_identity.h"
#include "iotc_time.h"
#include "quickstart_credentials.h"

#include "FacialDetect.h"
#include "iotc_thread.h"

LOG_MODULE_DECLARE(facedet, LOG_LEVEL_INF);

static K_SEM_DEFINE(l4_connected_sem, 0, 1);
static struct net_mgmt_event_callback l4_cb;
#define L4_EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

static void l4_event_handler(struct net_mgmt_event_callback *cb,
			     uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);
	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		LOG_INF("Network connectivity established (L4 up)");
		k_sem_give(&l4_connected_sem);
	} else if (mgmt_event == NET_EVENT_L4_DISCONNECTED) {
		LOG_WRN("Network connectivity lost (L4 down)");
	}
}

static int network_up(void)
{
	struct net_if *iface = net_if_get_default();

	if (iface == NULL) {
		return -ENODEV;
	}

#if DT_NODE_EXISTS(DT_ALIAS(modem))
	/* Power the cellular modem before bringing the PPP interface up. */
	const struct device *modem = DEVICE_DT_GET(DT_ALIAS(modem));

	if (pm_device_action_run(modem, PM_DEVICE_ACTION_RESUME) < 0) {
		LOG_WRN("modem resume failed (it may already be powered)");
	}
#endif

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
	LOG_INF("Waiting for cellular connectivity (registration + PPP)...");
	k_sem_take(&l4_connected_sem, K_FOREVER);
	return 0;
}

static void print_guide(const char *reason)
{
	printk("\n========================================================\n");
	printk("  face-detect: device is not provisioned (%s)\n", reason);
	printk("  1) iotcprov provision <your-duid>\n");
	printk("  2) IOTCONNECT: Create Device (Self-Signed, template\n");
	printk("     mcxn947-facedet) and paste the printed certificate\n");
	printk("  3) iotc config      (paste iotcDeviceConfig.json)\n");
	printk("  4) kernel reboot cold\n");
	printk("========================================================\n\n");
}

static void publish_faces(int faces)
{
	IotclMessageHandle msg = iotcl_telemetry_create();
	struct tm tm_utc;
	char ts[32];
	time_t now = time(NULL);

	if (msg == NULL) {
		return;
	}

	gmtime_r(&now, &tm_utc);
	strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tm_utc);

	iotcl_telemetry_set_number(msg, "faces", (double)faces);
	iotcl_telemetry_set_string(msg, "local_timestamp", ts);
	(void)iotcl_mqtt_send_telemetry(msg, false);
	iotcl_telemetry_destroy(msg);
	LOG_INF("reported faces=%d", faces);
}

void iotc_thread(void *config, void *fifo, void *dummy3)
{
	struct k_fifo *inf_results_fifo = fifo;
	struct iotc_identity id;
	int ret;

	ARG_UNUSED(config);
	ARG_UNUSED(dummy3);

	while (iotc_identity_load(&id) != 0) {
		print_guide("no identity stored in NVS");
		k_sleep(K_SECONDS(30));
	}
	LOG_INF("Provisioned as duid=%s -- bringing up cellular", id.duid);

	if (network_up() != 0) {
		LOG_ERR("network did not come up");
		return;
	}

	ret = iotc_time_sync(CONFIG_IOTCONNECT_SNTP_SERVER,
			     CONFIG_IOTCONNECT_SNTP_TIMEOUT_MS);
	if (ret) {
		LOG_WRN("SNTP sync failed (%d); continuing", ret);
	}

	IotConnectClientConfig cfg;

	iotconnect_sdk_init_config(&cfg);
	cfg.connection_type = IOTC_CT_AWS;
	cfg.cpid = (char *)id.cpid;
	cfg.env = (char *)id.env;
	cfg.duid = (char *)id.duid;
	cfg.auth_info.type = IOTC_AT_X509;
	cfg.auth_info.ca_cert = broker_ca_pem;		/* public roots, compiled in */
	cfg.auth_info.ca_cert_len = sizeof(broker_ca_pem);
	cfg.auth_info.dra_ca = dra_ca_pem;
	cfg.auth_info.dra_ca_len = sizeof(dra_ca_pem);
	cfg.auth_info.data.cert_info.device_cert = id.device_cert;	/* from NVS */
	cfg.auth_info.data.cert_info.device_cert_len = id.device_cert_len;
	cfg.auth_info.data.cert_info.device_key = id.device_key;	/* from NVS */
	cfg.auth_info.data.cert_info.device_key_len = id.device_key_len;
	cfg.verbose = true;

	ret = iotconnect_sdk_init(&cfg);
	if (ret) {
		LOG_ERR("iotconnect_sdk_init failed (%d)", ret);
		return;
	}

	while (true) {
		if (iotconnect_sdk_connect() != 0) {
			k_sleep(K_SECONDS(5));
			continue;
		}

		int64_t last_report = 0;
		int pending_faces = 0;
		bool have_pending = false;

		while (iotconnect_sdk_is_connected()) {
			inf_results_t *res =
				k_fifo_get(inf_results_fifo, K_SECONDS(1));

			if (res != NULL) {
				/* keep the latest count within the window */
				pending_faces = res->odRetCnt;
				have_pending = true;
			}

			int64_t now = k_uptime_get();
			int64_t period_ms =
				1000 * CONFIG_IOTC_MQTT_DEVICE_REPORT_SEC;

			if (have_pending && (now - last_report) >= period_ms) {
				publish_faces(pending_faces);
				last_report = now;
				have_pending = false;
				pending_faces = 0;
			} else if (!have_pending &&
				   (now - last_report) >= 3 * period_ms) {
				/* heartbeat: report zero when nothing seen */
				publish_faces(0);
				last_report = now;
			}
		}
		iotconnect_sdk_disconnect();
		LOG_WRN("Cloud connection lost; reconnecting...");
	}
}
