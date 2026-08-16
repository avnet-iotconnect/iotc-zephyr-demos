/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * gateway demo -- an IOTCONNECT gateway with offline store-and-forward, on
 * the NXP FRDM-i.MX93 (Cortex-A55) running Zephyr.
 *
 * Completes the repo's "telemetry source" arc: connectivity-less MCUs running
 * demos/uart-telemetry-source (FRDM-MCXE31B, FRDM-MCXW72) print IOTCONNECT
 * 2.1 JSON lines on a UART; this gateway ingests those lines, wraps each one
 * as a CHILD-DEVICE record ("id" + "tg" per the 2.1 gateway schema), adds its
 * own gateway telemetry + vitals, and publishes everything over one MQTT/TLS
 * connection.
 *
 * Resilience is the headline: when the uplink drops (pull the Ethernet
 * cable), every record -- gateway and children -- is spooled to a raw-sector
 * ring on the on-SOM eMMC with its original timestamp embedded. On reconnect
 * the spool drains oldest-first and the platform backfills the timeline.
 * Nothing is lost; the dashboard shows a gap-free history plus honest
 * spool/drop counters.
 *
 * Cloud commands: interval <s>, spool-info, spool-wipe, links, reboot.
 *
 * Child devices are created on the platform under this gateway device; each
 * UART link maps to one child (unique id + template tag) via Kconfig:
 *   CONFIG_GATEWAY_LINK0_CHILD_ID / CONFIG_GATEWAY_LINK0_TAG   (and LINK1).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>

#include <cJSON.h>

#include "iotconnect.h"
#include "iotcl.h"
#include "iotcl_telemetry.h"
#include "iotcl_util.h"
#include "iotcl_c2d.h"
#include "iotc_time.h"
#include "iotc_mqtt_client.h"      /* raw publish for child/spooled records */
#include "iotconnect_identity.h"
#if defined(CONFIG_IOTCONNECT_DEVICE_VITALS)
#include "iotconnect_vitals.h"
#endif
#include "quickstart_credentials.h" /* PUBLIC CA roots only (no device key) */

#include "uart_ingest.h"
#include "spool.h"

LOG_MODULE_REGISTER(gateway, LOG_LEVEL_INF);

#define RECONNECT_PERIOD_SEC 20
#define DRAIN_PER_TICK 4
#define ISO_TS_LEN 32

static volatile int publish_interval_sec = 10;
static bool spool_ok;
static uint32_t fwd_total;          /* child records forwarded (live) */

static const struct {
	const char *child_id;
	const char *tag;
} link_map[INGEST_MAX_LINKS] = {
	{ CONFIG_GATEWAY_LINK0_CHILD_ID, CONFIG_GATEWAY_LINK0_TAG },
	{ CONFIG_GATEWAY_LINK1_CHILD_ID, CONFIG_GATEWAY_LINK1_TAG },
};

static void print_guide(const char *reason)
{
	printk("\n");
	printk("========================================================\n");
	printk("  IOTCONNECT GATEWAY -- device is not provisioned\n");
	printk("  (%s)\n", reason);
	printk("--------------------------------------------------------\n");
	printk("  1) Generate this device's identity ON the device:\n");
	printk("        iotcprov provision <your-duid>\n");
	printk("  2) In IOTCONNECT: import the gateway template, Create\n");
	printk("     Device (Self-Signed, GATEWAY) on it, and paste the\n");
	printk("     certificate printed above. Create the child devices\n");
	printk("     under it (see README).\n");
	printk("  3) Paste the downloaded iotcDeviceConfig.json:\n");
	printk("        iotc config\n");
	printk("        { ...paste the json block... }\n");
	printk("  4) Connect:  kernel reboot cold\n");
	printk("========================================================\n\n");
}

/* --- Publishing ------------------------------------------------------------ */

static void publish_raw(const char *json)
{
	IotclMqttConfig *mc = iotcl_mqtt_get_config();

	if (mc != NULL && mc->pub_rpt != NULL) {
		iotc_mqtt_client_publish(mc->pub_rpt, json);
	}
}

/*
 * Derive the platform child Unique ID from the source's self-reported board
 * name: strip separators (the platform allows only alphanumerics in child
 * IDs) and append "01", mirroring the source's "<board>-01" duid convention.
 * "frdm_mcxe31b" -> "frdmmcxe31b01". This makes the single wired link
 * board-agnostic: whichever source MCU is plugged in shows up as itself.
 */
static void child_id_from_board(const char *board, char *out, size_t out_size)
{
	size_t o = 0;

	for (; *board != '\0' && o < out_size - 3; board++) {
		char c = *board;

		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
		    (c >= '0' && c <= '9')) {
			out[o++] = c;
		}
	}
	out[o++] = '0';
	out[o++] = '1';
	out[o] = '\0';
}

/*
 * Wrap one source line (a 2.1 message from uart-telemetry-source, shape
 * {"d":[{"d":{...}}]}, or any bare JSON object) as a gateway CHILD record:
 *   {"d":[{"id":"<child>","tg":"<tag>","dt":"<iso>","d":{...}}]}
 * The child id comes from the record's own "board" field when present
 * (see child_id_from_board), else the link's Kconfig default.
 * Returns a malloc'd string (cJSON_free() it) or NULL on parse failure.
 */
static char *build_child_record(int link, const char *src_json)
{
	cJSON *src = cJSON_Parse(src_json);
	cJSON *data = NULL;
	char *out = NULL;

	if (src == NULL) {
		return NULL;
	}

	/* Locate the inner data object; tolerate both envelope and bare form. */
	if (cJSON_IsObject(src)) {
		cJSON *arr = cJSON_GetObjectItem(src, "d");

		if (cJSON_IsArray(arr) && cJSON_GetArraySize(arr) > 0) {
			cJSON *item0 = cJSON_GetArrayItem(arr, 0);
			cJSON *inner = cJSON_GetObjectItem(item0, "d");

			if (cJSON_IsObject(inner)) {
				data = cJSON_DetachItemFromObject(item0, "d");
			}
		} else if (arr == NULL) {
			data = src; /* bare object: use it wholesale */
			src = NULL;
		}
	}
	if (data == NULL) {
		cJSON_Delete(src);
		return NULL;
	}

	cJSON *root = cJSON_CreateObject();
	cJSON *darr = cJSON_AddArrayToObject(root, "d");
	cJSON *item = cJSON_CreateObject();
	char ts[ISO_TS_LEN];
	char derived_id[48];
	const char *child_id = link_map[link].child_id;
	const char *board = cJSON_GetStringValue(
		cJSON_GetObjectItem(data, "board"));

	if (board != NULL && board[0] != '\0') {
		child_id_from_board(board, derived_id, sizeof(derived_id));
		child_id = derived_id;
	}
	cJSON_AddStringToObject(item, "id", child_id);
	if (link_map[link].tag[0] != '\0') {
		cJSON_AddStringToObject(item, "tg", link_map[link].tag);
	}
	if (iotcl_iso_timestamp_now(ts, sizeof(ts)) == 0) {
		cJSON_AddStringToObject(item, "dt", ts);
	}
	cJSON_AddItemToObject(item, "d", data);
	cJSON_AddItemToArray(darr, item);

	out = cJSON_PrintUnformatted(root);
	cJSON_Delete(root);
	cJSON_Delete(src);
	return out;
}

/* Gateway-own record: link health, spool state, vitals. Publishes live when
 * connected, otherwise serializes and spools with its timestamp. */
static void publish_or_spool_gateway(bool connected)
{
	struct spool_stats sp;
	struct ingest_stats st;
	char key[24];

	spool_get_stats(&sp);

	IotclMessageHandle msg = iotcl_telemetry_create();

	if (msg == NULL) {
		return;
	}
	iotcl_telemetry_set_number(msg, "gw.online", connected ? 1 : 0);
	iotcl_telemetry_set_number(msg, "gw.links", uart_ingest_link_count());
	iotcl_telemetry_set_number(msg, "gw.fwd_total", (double)fwd_total);
	/* iotc-c-lib telemetry paths allow at most ONE dot (hardware-observed
	 * rejection of deeper nesting), so spool/link fields are flattened. */
	iotcl_telemetry_set_number(msg, "gw.spool_count", (double)sp.count);
	iotcl_telemetry_set_number(msg, "gw.spool_pushed", (double)sp.pushed);
	iotcl_telemetry_set_number(msg, "gw.spool_drained", (double)sp.drained);
	iotcl_telemetry_set_number(msg, "gw.spool_drops", (double)sp.drops);

	for (int i = 0; i < INGEST_MAX_LINKS; i++) {
		uart_ingest_get_stats(i, &st);
		if (st.lines_ok == 0 && st.lines_dropped == 0) {
			continue;
		}
		snprintf(key, sizeof(key), "gw.link%d_rx", i);
		iotcl_telemetry_set_number(msg, key, (double)st.lines_ok);
		snprintf(key, sizeof(key), "gw.link%d_drop", i);
		iotcl_telemetry_set_number(msg, key, (double)st.lines_dropped);
		snprintf(key, sizeof(key), "gw.link%d_age_s", i);
		iotcl_telemetry_set_number(msg, key, st.last_seen_ms > 0 ?
			(double)(k_uptime_get() - st.last_seen_ms) / 1000.0 : -1.0);
	}
#if defined(CONFIG_IOTCONNECT_DEVICE_VITALS)
	iotc_vitals_append(msg);
#endif

	/*
	 * Gateway-template attributes are TAG-scoped; the platform binds a
	 * record's values only when the record carries the matching "tg"
	 * (hardware-observed: tg-less gateway records display all-null).
	 * iotcl has no tg support, so serialize, inject tg into the record,
	 * and publish/spool the raw JSON.
	 */
	char *json = iotcl_telemetry_create_serialized_string(msg, false);

	iotcl_telemetry_destroy(msg);
	if (json == NULL) {
		return;
	}

	cJSON *root = cJSON_Parse(json);

	iotcl_telemetry_destroy_serialized_string(json);
	if (root == NULL) {
		return;
	}

	cJSON *item0 = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "d"), 0);

	if (item0 != NULL) {
		cJSON_AddStringToObject(item0, "tg", CONFIG_GATEWAY_SELF_TAG);
	}

	char *out = cJSON_PrintUnformatted(root);

	cJSON_Delete(root);
	if (out == NULL) {
		return;
	}
	if (connected) {
		publish_raw(out);
	} else if (spool_ok) {
		(void)spool_push(out);
	}
	cJSON_free(out);
}

/* --- C2D commands ----------------------------------------------------------- */

static bool tok_is(const char *tok, size_t len, const char *name)
{
	if (strlen(name) != len) {
		return false;
	}
	for (size_t i = 0; i < len; i++) {
		if (tolower((unsigned char)tok[i]) != (unsigned char)name[i]) {
			return false;
		}
	}
	return true;
}

static void on_command(IotclC2dEventData data)
{
	const char *cmd = iotcl_c2d_get_command(data);
	const char *ack = iotcl_c2d_get_ack_id(data);
	int status = IOTCL_C2D_EVT_CMD_FAILED;
	char note[128] = "unknown command";

	LOG_INF("C2D command: %.64s", cmd ? cmd : "(null)");
	if (cmd == NULL) {
		goto out;
	}
	while (*cmd == ' ') {
		cmd++;
	}
	const char *sp = strchr(cmd, ' ');
	size_t name_len = (sp != NULL) ? (size_t)(sp - cmd) : strlen(cmd);
	const char *arg = (sp != NULL) ? sp + 1 : NULL;

	while (arg != NULL && *arg == ' ') {
		arg++;
	}
	if (arg != NULL && *arg == '\0') {
		arg = NULL;
	}

	if (tok_is(cmd, name_len, "interval") && arg != NULL) {
		int v = atoi(arg);

		if (v >= 1 && v <= 3600) {
			publish_interval_sec = v;
			status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
			snprintf(note, sizeof(note), "interval=%ds", v);
		} else {
			snprintf(note, sizeof(note), "interval out of range");
		}
	} else if (tok_is(cmd, name_len, "spool-info")) {
		struct spool_stats sp;

		spool_get_stats(&sp);
		snprintf(note, sizeof(note),
			 "%u/%u queued, pushed=%u drained=%u drops=%u%s",
			 (unsigned int)sp.count, (unsigned int)sp.capacity,
			 (unsigned int)sp.pushed, (unsigned int)sp.drained,
			 (unsigned int)sp.drops,
			 spool_ok ? "" : " (SPOOL OFFLINE)");
		status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
	} else if (tok_is(cmd, name_len, "spool-wipe")) {
		if (spool_wipe() == 0) {
			status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
			snprintf(note, sizeof(note), "spool wiped");
		} else {
			snprintf(note, sizeof(note), "spool wipe failed");
		}
	} else if (tok_is(cmd, name_len, "links")) {
		struct ingest_stats s0, s1;

		uart_ingest_get_stats(0, &s0);
		uart_ingest_get_stats(1, &s1);
		snprintf(note, sizeof(note),
			 "link0(%s->%s): rx=%u drop=%u | link1(%s->%s): rx=%u drop=%u",
			 uart_ingest_link_name(0), link_map[0].child_id,
			 (unsigned int)s0.lines_ok, (unsigned int)s0.lines_dropped,
			 uart_ingest_link_name(1), link_map[1].child_id,
			 (unsigned int)s1.lines_ok, (unsigned int)s1.lines_dropped);
		status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
	} else if (tok_is(cmd, name_len, "reboot")) {
		if (ack != NULL) {
			(void)iotcl_mqtt_send_cmd_ack(
				ack, IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK, NULL);
		}
		k_sleep(K_MSEC(500));
		sys_reboot(SYS_REBOOT_COLD);
	}

out:
	if (ack != NULL) {
		(void)iotcl_mqtt_send_cmd_ack(ack, status, note);
	}
}

static void on_connection_status(IotConnectMqttStatus status)
{
	LOG_INF("MQTT status: %d", (int)status);
}

/* --- Network bring-up ------------------------------------------------------- */

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
		LOG_WRN("Network connectivity lost (L4 down) -- spooling");
	}
}

static int network_up(void)
{
	struct net_if *iface = net_if_get_default();

	if (iface == NULL) {
		LOG_ERR("No default network interface");
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

/* --- Main ------------------------------------------------------------------- */

static char line_buf[INGEST_LINE_MAX];
static char spool_out[SPOOL_PAYLOAD_MAX + 1];

/* Drain queued source lines from every link; forward live or spool. */
static void ingest_children(bool connected)
{
	for (int link = 0; link < INGEST_MAX_LINKS; link++) {
		int n;

		while ((n = uart_ingest_pop(link, line_buf,
					    sizeof(line_buf))) > 0) {
			char *rec = build_child_record(link, line_buf);

			if (rec == NULL) {
				LOG_WRN("link%d: unparseable line dropped", link);
				continue;
			}
			if (connected) {
				publish_raw(rec);
				fwd_total++;
			} else if (spool_ok) {
				(void)spool_push(rec);
			}
			cJSON_free(rec);
		}
	}
}

int main(void)
{
	int ret;

	LOG_INF("gateway demo starting (%d UART link(s) in devicetree)",
		uart_ingest_link_count());

	if (uart_ingest_init() != 0) {
		LOG_ERR("UART ingest init failed");
		return 0;
	}
	spool_ok = spool_init() == 0;
	if (!spool_ok) {
		LOG_WRN("eMMC spool unavailable -- offline records will be LOST");
	}

	struct iotc_identity id;

	if (iotc_identity_load(&id) != 0) {
		print_guide("no identity stored");
		return 0; /* stay alive at the shell for provisioning */
	}
	LOG_INF("Provisioned as duid=%s -- bringing up network", id.duid);

	ret = network_up();
	if (ret) {
		return 0;
	}
	ret = iotc_time_sync(CONFIG_IOTCONNECT_SNTP_SERVER,
			     CONFIG_IOTCONNECT_SNTP_TIMEOUT_MS);
	if (ret) {
		LOG_ERR("SNTP sync failed (%d); TLS will likely fail", ret);
		return 0;
	}

	IotConnectClientConfig config;

	iotconnect_sdk_init_config(&config);
#if defined(CONFIG_IOTCONNECT_CT_AWS)
	config.connection_type = IOTC_CT_AWS;
#elif defined(CONFIG_IOTCONNECT_CT_AZURE)
	config.connection_type = IOTC_CT_AZURE;
#endif
	config.cpid = (char *)id.cpid;
	config.env = (char *)id.env;
	config.duid = (char *)id.duid;

	config.auth_info.type = IOTC_AT_X509;
	config.auth_info.ca_cert = broker_ca_pem;
	config.auth_info.ca_cert_len = sizeof(broker_ca_pem);
	config.auth_info.dra_ca = dra_ca_pem;
	config.auth_info.dra_ca_len = sizeof(dra_ca_pem);
	config.auth_info.data.cert_info.device_cert = id.device_cert;
	config.auth_info.data.cert_info.device_cert_len = id.device_cert_len;
	config.auth_info.data.cert_info.device_key = id.device_key;
	config.auth_info.data.cert_info.device_key_len = id.device_key_len;

	config.status_cb = on_connection_status;
	config.cmd_cb = on_command;
	config.verbose = true;

	ret = iotconnect_sdk_init(&config);
	if (ret) {
		LOG_ERR("iotconnect_sdk_init failed (%d)", ret);
		return 0;
	}

	/*
	 * Resilient main loop: unlike the point-demos, a lost connection does
	 * NOT pause the work -- ingest and the periodic gateway record keep
	 * running, records divert to the eMMC spool, and reconnects are
	 * attempted in the background of the same loop.
	 */
	bool was_connected = false;
	int64_t last_pub = 0, last_reconnect = 0;

	while (true) {
		int64_t now = k_uptime_get();
		bool connected = iotconnect_sdk_is_connected();

		if (!connected &&
		    (last_reconnect == 0 ||
		     now - last_reconnect >= RECONNECT_PERIOD_SEC * 1000)) {
			if (was_connected) {
				iotconnect_sdk_disconnect();
				was_connected = false;
			}
			last_reconnect = now;
			LOG_INF("connecting...");
			if (iotconnect_sdk_connect() == 0) {
				connected = true;
				LOG_INF("Connected. Try: links, spool-info; "
					"then pull the cable.");
			} else {
				struct spool_stats sptmp;

				spool_get_stats(&sptmp);
				LOG_WRN("connect failed; %u records spooled so far",
					(unsigned int)sptmp.count);
			}
		}
		if (connected) {
			was_connected = true;
		}

		ingest_children(connected);

		if (last_pub == 0 ||
		    now - last_pub >= (int64_t)publish_interval_sec * 1000) {
			publish_or_spool_gateway(connected);
			last_pub = now;
		}

		if (connected && spool_ok) {
			for (int i = 0; i < DRAIN_PER_TICK; i++) {
				int n = spool_peek(spool_out, sizeof(spool_out));

				if (n <= 0) {
					break;
				}
				publish_raw(spool_out);
				(void)spool_commit();
			}
		}

		k_sleep(K_MSEC(500));
	}
	return 0;
}
