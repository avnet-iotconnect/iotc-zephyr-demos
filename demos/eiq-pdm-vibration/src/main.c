/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * eIQ predictive-maintenance -- vibration (FRDM-MCXN947 + MIKROE ML Vibro Sens
 * Click: FXLS8974CF accelerometer + two onboard DC motors).
 *
 * Two build modes share the same sensor + motor code:
 *
 *  - CAPTURE (default, network-free): drive the motors through a labeled state
 *    cycle and print CSV (VIB,t_ms,state,ax,ay,az) for eIQ Time Series Studio
 *    training. (Phase 1.)
 *
 *  - CONNECT (CONFIG_IOTCONNECT=y, via -DEXTRA_CONF_FILE=connect.conf): connect
 *    to /IOTCONNECT, window the accelerometer, classify the machine state
 *    (RMS-threshold heuristic today; drop in the eIQ Neutron-NPU model later),
 *    and publish vib.* telemetry (+ the sys device-vitals sidecar). C2D commands
 *    drive the motors -- inject-fault / inject-healthy / motor-stop -- so a fault
 *    can be injected from the cloud and watched being detected. (Phase 2.)
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>
#include <math.h>

#define SAMPLE_HZ   100
#define SAMPLE_MS   (1000 / SAMPLE_HZ)
#define G_PER_MS2   (1.0 / 9.80665)   /* Zephyr accel channels are m/s^2 */

enum vib_state { ST_IDLE, ST_BALANCED, ST_UNBALANCED, ST_BOTH, ST_COUNT };
static const char *const state_name[ST_COUNT] = {
	"idle", "balanced", "unbalanced", "both",
};

static const struct device *const accel = DEVICE_DT_GET(DT_NODELABEL(fxls8974));
static const struct gpio_dt_spec bal =
	GPIO_DT_SPEC_GET(DT_NODELABEL(bal_motor), gpios);
static const struct gpio_dt_spec unb =
	GPIO_DT_SPEC_GET(DT_NODELABEL(unb_motor), gpios);

static bool read_accel(double g[3])
{
	struct sensor_value a[3];

	if (sensor_sample_fetch(accel) != 0 ||
	    sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, a) != 0) {
		return false;
	}
	g[0] = sensor_value_to_double(&a[0]) * G_PER_MS2;
	g[1] = sensor_value_to_double(&a[1]) * G_PER_MS2;
	g[2] = sensor_value_to_double(&a[2]) * G_PER_MS2;
	return true;
}

/* Apply a motor state. The unbalanced motor is software-pulsed (~30%% duty at
 * 5 Hz via phase_ms) so it is never held at continuous full power. */
static void motors_apply(enum vib_state st, int64_t phase_ms)
{
	bool unb_active = (st == ST_UNBALANCED || st == ST_BOTH);

	gpio_pin_set_dt(&bal, (st == ST_BALANCED || st == ST_BOTH) ? 1 : 0);
	gpio_pin_set_dt(&unb, (unb_active && (phase_ms % 200) < 60) ? 1 : 0);
}

static int hw_init(void)
{
	if (!device_is_ready(accel)) {
		printk("ERROR: FXLS8974 accelerometer not ready\n");
		return -1;
	}
	if (!gpio_is_ready_dt(&bal) || !gpio_is_ready_dt(&unb)) {
		printk("ERROR: vibro-motor GPIOs not ready\n");
		return -1;
	}
	gpio_pin_configure_dt(&bal, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&unb, GPIO_OUTPUT_INACTIVE);

	/* Raise the output data rate. The driver defaults to 6.25 Hz, and the
	 * FXLS8974 only accepts an ODR change in STANDBY, so drop to standby, set
	 * the rate on SENSOR_CHAN_ALL, then re-activate. Without this the sensor
	 * repeats each reading ~16x under a 100 Hz poll -> useless for vibration. */
	extern int fxls8974_set_active(const struct device *dev, uint8_t active);
	struct sensor_value odr = { .val1 = 400, .val2 = 0 };

	(void)fxls8974_set_active(accel, 0x00);   /* standby (ACTIVE bit clear) */
	if (sensor_attr_set(accel, SENSOR_CHAN_ALL,
			    SENSOR_ATTR_SAMPLING_FREQUENCY, &odr) != 0) {
		printk("WARN: could not set ODR (400 Hz); using sensor default\n");
	}
	(void)fxls8974_set_active(accel, 0x01);   /* active */
	return 0;
}

#if !defined(CONFIG_IOTCONNECT)
/* ======================= CAPTURE mode (Phase 1) ========================= */
#define STATE_MS 4000

int main(void)
{
	printk("\n=== eIQ PdM vibration capture (FXLS8974 + onboard motors) ===\n");
	printk("Labeled motor-state cycle @~%d Hz. CSV: VIB,t_ms,state,ax,ay,az\n\n",
	       SAMPLE_HZ);

	if (hw_init()) {
		return 0;
	}
	printk("VIB,t_ms,state,ax_g,ay_g,az_g\n");

	int last = -1;

	while (1) {
		int64_t t = k_uptime_get();
		enum vib_state st = (t / STATE_MS) % ST_COUNT;

		if ((int)st != last) {
			last = st;
			printk("# state -> %s\n", state_name[st]);
		}
		motors_apply(st, t);

		double g[3];

		if (read_accel(g)) {
			printk("VIB,%lld,%s,%.4f,%.4f,%.4f\n", t, state_name[st],
			       g[0], g[1], g[2]);
		}
		k_msleep(SAMPLE_MS);
	}
	return 0;
}

#else
/* ======================= CONNECT mode (Phase 2) ========================= */
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/sys/reboot.h>
#include <ctype.h>
#include <stdlib.h>

#include "iotconnect.h"
#include "iotcl.h"
#include "iotcl_telemetry.h"
#include "iotcl_c2d.h"
#include "iotc_time.h"
#if defined(CONFIG_IOTCONNECT_DEVICE_VITALS)
#include "iotconnect_vitals.h"
#endif
#include "device_credentials.h"   /* baked device cert/key + CA roots (cpu0) */

LOG_MODULE_REGISTER(eiq_pdm, LOG_LEVEL_INF);

static volatile enum vib_state demo_state = ST_IDLE; /* driven by C2D commands */
static int publish_interval_sec = 2;
static double thresh_fault = 0.35;   /* overall g-rms -> "fault"   */
static double thresh_warn  = 0.12;   /* overall g-rms -> "warning" */

/* Collect a ~1 s window of accelerometer while driving the current motor state;
 * return per-axis RMS of the AC component + overall vibration magnitude (g). */
static void vib_window(double rms[3], double *overall)
{
	enum vib_state st = demo_state;
	double sum[3] = {0}, sumsq[3] = {0};
	int n = 0;
	int64_t t0 = k_uptime_get();

	while (k_uptime_get() - t0 < 1000) {
		double g[3];

		motors_apply(st, k_uptime_get());
		if (read_accel(g)) {
			for (int i = 0; i < 3; i++) {
				sum[i] += g[i];
				sumsq[i] += g[i] * g[i];
			}
			n++;
		}
		k_msleep(SAMPLE_MS);
	}

	double var_sum = 0;

	for (int i = 0; i < 3; i++) {
		double mean = n ? sum[i] / n : 0;
		double var = n ? (sumsq[i] / n - mean * mean) : 0;

		rms[i] = (var > 0) ? sqrt(var) : 0;
		var_sum += (var > 0) ? var : 0;
	}
	*overall = sqrt(var_sum);
}

static const char *classify(double overall, double *score)
{
	double s = (overall - 0.03) / (thresh_fault - 0.03);

	*score = (s < 0) ? 0 : (s > 1) ? 1 : s;
	if (overall >= thresh_fault) {
		return "fault";
	}
	if (overall >= thresh_warn) {
		return "warning";
	}
	return "healthy";
}

static void publish_vib(void)
{
	double rms[3], overall, score;

	vib_window(rms, &overall);

	const char *st = classify(overall, &score);
	IotclMessageHandle msg = iotcl_telemetry_create();

	if (msg == NULL) {
		return;
	}
	iotcl_telemetry_set_string(msg, "vib.state", st);
	iotcl_telemetry_set_number(msg, "vib.anomaly_score", score);
	iotcl_telemetry_set_number(msg, "vib.rms_g", overall);
	iotcl_telemetry_set_number(msg, "vib.rms_x", rms[0]);
	iotcl_telemetry_set_number(msg, "vib.rms_y", rms[1]);
	iotcl_telemetry_set_number(msg, "vib.rms_z", rms[2]);
	iotcl_telemetry_set_string(msg, "vib.motor", state_name[demo_state]);
#if defined(CONFIG_IOTCONNECT_DEVICE_VITALS)
	iotc_vitals_append(msg);
#endif
	(void)iotcl_mqtt_send_telemetry(msg, false);
	iotcl_telemetry_destroy(msg);
	LOG_INF("vib: state=%s score=%.2f rms=%.3f (driving %s)",
		st, score, overall, state_name[demo_state]);
}

/* --- C2D command handling ------------------------------------------------- */

static bool ci_contains(const char *haystack, const char *needle)
{
	for (const char *h = haystack; *h != '\0'; h++) {
		size_t i = 0;

		while (needle[i] != '\0' &&
		       tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i])) {
			i++;
		}
		if (needle[i] == '\0') {
			return true;
		}
	}
	return needle[0] == '\0';
}

static void on_command(IotclC2dEventData data)
{
	const char *cmd = iotcl_c2d_get_command(data);
	const char *ack = iotcl_c2d_get_ack_id(data);
	int status = IOTCL_C2D_EVT_CMD_FAILED;

	LOG_INF("C2D command: %s", cmd ? cmd : "(null)");
	if (cmd != NULL) {
		if (ci_contains(cmd, "fault")) {          /* inject-fault */
			demo_state = ST_UNBALANCED;
			status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
		} else if (ci_contains(cmd, "healthy")) { /* inject-healthy */
			demo_state = ST_BALANCED;
			status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
		} else if (ci_contains(cmd, "both")) {
			demo_state = ST_BOTH;
			status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
		} else if (ci_contains(cmd, "stop") || ci_contains(cmd, "idle") ||
			   ci_contains(cmd, "off")) {
			demo_state = ST_IDLE;
			status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
		} else if (ci_contains(cmd, "threshold")) {
			double v = atof(strpbrk(cmd, "0123456789") ?: "");

			if (v > 0.0 && v < 2.0) {
				thresh_fault = v;
				status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
			}
		} else if (ci_contains(cmd, "interval")) {
			int v = atoi(strpbrk(cmd, "0123456789") ?: "");

			if (v >= 1 && v <= 3600) {
				publish_interval_sec = v;
				status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
			}
		} else if (ci_contains(cmd, "reboot")) {
			if (ack != NULL) {
				(void)iotcl_mqtt_send_cmd_ack(
					ack, IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK, NULL);
			}
			k_sleep(K_MSEC(500));
			sys_reboot(SYS_REBOOT_COLD);
		}
	}
	if (ack != NULL) {
		(void)iotcl_mqtt_send_cmd_ack(ack, status,
			status == IOTCL_C2D_EVT_CMD_FAILED ? "unknown command" : NULL);
	}
}

/* --- Network bring-up ----------------------------------------------------- */

static K_SEM_DEFINE(l4_connected_sem, 0, 1);
static struct net_mgmt_event_callback l4_cb;
#define L4_EVENT_MASK (NET_EVENT_L4_CONNECTED | NET_EVENT_L4_DISCONNECTED)

static void l4_event_handler(struct net_mgmt_event_callback *cb,
			     uint64_t mgmt_event, struct net_if *iface)
{
	ARG_UNUSED(cb);
	ARG_UNUSED(iface);
	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		k_sem_give(&l4_connected_sem);
	}
}

static int network_up(void)
{
	struct net_if *iface = net_if_get_default();

	if (iface == NULL) {
		return -ENODEV;
	}
	net_mgmt_init_event_callback(&l4_cb, l4_event_handler, L4_EVENT_MASK);
	net_mgmt_add_event_callback(&l4_cb);
	if (!net_if_is_up(iface)) {
		int ret = net_if_up(iface);

		if (ret && ret != -EALREADY) {
			return ret;
		}
	}
	conn_mgr_mon_resend_status();
	LOG_INF("Waiting for network connectivity...");
	k_sem_take(&l4_connected_sem, K_FOREVER);
	return 0;
}

int main(void)
{
	int ret;

	LOG_INF("eIQ PdM vibration monitor starting");
	if (hw_init()) {
		return 0;
	}
	if (network_up() != 0) {
		return 0;
	}
	ret = iotc_time_sync(CONFIG_IOTCONNECT_SNTP_SERVER,
			     CONFIG_IOTCONNECT_SNTP_TIMEOUT_MS);
	if (ret) {
		LOG_ERR("SNTP sync failed (%d)", ret);
		return 0;
	}

	IotConnectClientConfig config;

	iotconnect_sdk_init_config(&config);
#if defined(CONFIG_IOTCONNECT_CT_AWS)
	config.connection_type = IOTC_CT_AWS;
#elif defined(CONFIG_IOTCONNECT_CT_AZURE)
	config.connection_type = IOTC_CT_AZURE;
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
	config.cmd_cb = on_command;
	config.verbose = true;

	ret = iotconnect_sdk_init(&config);
	if (ret) {
		LOG_ERR("iotconnect_sdk_init failed (%d)", ret);
		return 0;
	}

	while (true) {
		ret = iotconnect_sdk_connect();
		if (ret) {
			LOG_ERR("connect failed (%d); retrying", ret);
			k_sleep(K_SECONDS(5));
			continue;
		}
		LOG_INF("Connected. Send C2D 'inject-fault' / 'inject-healthy' / "
			"'motor-stop' to drive the demo.");
		while (iotconnect_sdk_is_connected()) {
			publish_vib();
			k_sleep(K_SECONDS(publish_interval_sec));
		}
		iotconnect_sdk_disconnect();
		LOG_WRN("Disconnected; reconnecting...");
	}
	return 0;
}
#endif /* CONFIG_IOTCONNECT */
