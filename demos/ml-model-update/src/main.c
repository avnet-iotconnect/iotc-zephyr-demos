/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * ml-model-update demo -- cloud-updatable ML on a Microchip SAM E54 + IO1
 * Xplained Pro, over Avnet /IOTCONNECT.
 *
 * The firmware ships a FIXED tiny-MLP inference engine; the MODEL is pure
 * data (an "IOTM" blob: header + float32 weights, ~124 bytes for 2-2-3).
 * Every second the app reads the IO1's sensors (AT30TSE758 temperature,
 * TEMT6000 light), runs the active model, drives the IO1 LED from the
 * predicted class, and publishes io1.* / ml.* telemetry.
 *
 * A NEW MODEL ARRIVES FROM THE CLOUD AS DATA -- no reflash, no reboot:
 *   model-push <base64-blob>      one-shot install (fits one C2D command)
 *   model-begin / model-data <b64-chunk> ... / model-commit
 *                                 chunked path for larger models
 *   model-info                    ACK with the active model's identity
 *   model-reset                   revert to the built-in model
 * Installed models are validated (magic/format/dims/CRC32), hot-swapped
 * under a lock, and persisted to NVS settings so they survive reboot.
 *
 * Also: led-on / led-off / led-auto (auto = model drives the LED via the
 * blob's led_mask), interval <sec>, reboot. Every command is ACKed.
 *
 * Demo script: the built-in v1 "ambient" model classifies LIGHT
 * (dark/dim/bright; LED = bright). Push models/model_v2_comfort.cmd and the
 * same device starts classifying TEMPERATURE (cool/comfy/warm; LED = warm)
 * -- same firmware, new behavior, pushed from the dashboard.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/base64.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>

#include "iotconnect.h"
#include "iotcl.h"
#include "iotcl_telemetry.h"
#include "iotcl_c2d.h"
#include "iotc_time.h"
#include "iotc_dra_client.h"       /* iotc_https_download() for model URLs */
#include "iotconnect_identity.h"   /* NVS-provisioned device identity */
#if defined(CONFIG_IOTCONNECT_DEVICE_VITALS)
#include "iotconnect_vitals.h"
#endif
#include "quickstart_credentials.h" /* PUBLIC CA roots only (no device key) */
#include "model_builtin.h"          /* generated: tools/build_model.py */

LOG_MODULE_REGISTER(ml_model_update, LOG_LEVEL_INF);

static void print_guide(const char *reason)
{
	printk("\n");
	printk("========================================================\n");
	printk("  ML MODEL UPDATE -- device is not provisioned\n");
	printk("  (%s)\n", reason);
	printk("--------------------------------------------------------\n");
	printk("  1) Generate this device's identity ON the device:\n");
	printk("        iotcprov provision <your-duid>\n");
	printk("  2) In IOTCONNECT: import the ml-model-update template,\n");
	printk("     Create Device (Self-Signed) on it, and paste the\n");
	printk("     certificate printed above.\n");
	printk("  3) Paste the downloaded iotcDeviceConfig.json:\n");
	printk("        iotc config\n");
	printk("        { ...paste the json block... }\n");
	printk("  4) Connect:  kernel reboot cold\n");
	printk("========================================================\n\n");
}

/* --- IO1 Xplained Pro hardware (EXT1) -------------------------------------- */

#define AT30TSE_ADDR      0x4F    /* temp sensor; A2..A0 pulled high on IO1 */
#define AT30TSE_REG_TEMP  0x00
#define AT30TSE_REG_CFG   0x01

static const struct device *const io1_i2c = DEVICE_DT_GET(DT_ALIAS(io1_i2c));
static const struct device *const adc = DEVICE_DT_GET(DT_NODELABEL(adc1));
static const struct gpio_dt_spec io1_led = GPIO_DT_SPEC_GET(DT_ALIAS(io1_led),
							    gpios);

#define LIGHT_ADC_CHANNEL   0
#define LIGHT_ADC_INPUT     6      /* ADC1/AIN[6] = PB04 = EXT1 pin 3 */
#define LIGHT_ADC_RES       12

static int16_t adc_sample;
static struct adc_sequence adc_seq = {
	.channels = BIT(LIGHT_ADC_CHANNEL),
	.buffer = &adc_sample,
	.buffer_size = sizeof(adc_sample),
	.resolution = LIGHT_ADC_RES,
};

static int io1_init(void)
{
	/* AT30TSE758 -> 12-bit resolution (config register, bits 14:13 = 11). */
	uint8_t cfg[3] = { AT30TSE_REG_CFG, 0x60, 0x00 };
	struct adc_channel_cfg ch = {
		.gain = ADC_GAIN_1,
		.reference = ADC_REF_VDD_1,
		.acquisition_time = ADC_ACQ_TIME_DEFAULT,
		.channel_id = LIGHT_ADC_CHANNEL,
		.input_positive = LIGHT_ADC_INPUT,
	};
	int ret;

	if (!device_is_ready(io1_i2c) || !device_is_ready(adc) ||
	    !gpio_is_ready_dt(&io1_led)) {
		LOG_ERR("IO1 devices not ready: i2c(%s)=%d adc(%s)=%d led(%s)=%d",
			io1_i2c->name, device_is_ready(io1_i2c),
			adc->name, device_is_ready(adc),
			io1_led.port->name, gpio_is_ready_dt(&io1_led));
		return -ENODEV;
	}
	gpio_pin_configure_dt(&io1_led, GPIO_OUTPUT_INACTIVE);

	ret = i2c_write(io1_i2c, cfg, sizeof(cfg), AT30TSE_ADDR);
	if (ret) {
		LOG_WRN("AT30TSE758 not responding at 0x%02X (%d) -- is the "
			"IO1 on EXT1?", AT30TSE_ADDR, ret);
	}
	ret = adc_channel_setup(adc, &ch);
	if (ret) {
		LOG_ERR("ADC channel setup failed (%d)", ret);
	}
	return 0;
}

static int read_temp_c(double *temp_c)
{
	uint8_t reg = AT30TSE_REG_TEMP, b[2];
	int ret = i2c_write_read(io1_i2c, AT30TSE_ADDR, &reg, 1, b, sizeof(b));

	if (ret) {
		return ret;
	}
	/* 12-bit left-justified two's complement, 0.0625 C/LSB. */
	*temp_c = (double)((int16_t)((b[0] << 8) | b[1]) >> 4) * 0.0625;
	return 0;
}

static int read_light_pct(double *light_pct)
{
	int ret = adc_read(adc, &adc_seq);

	if (ret) {
		return ret;
	}
	if (adc_sample < 0) {
		adc_sample = 0;
	} else if (adc_sample > (int16_t)BIT_MASK(LIGHT_ADC_RES)) {
		adc_sample = BIT_MASK(LIGHT_ADC_RES);
	}
	/* The IO1's TEMT6000 pulls its output node LOW as light rises (load to
	 * VCC above the collector), so the raw count FALLS with brightness --
	 * covered sensor reads near full scale (hardware-observed). Invert so
	 * light_pct rises with illumination. */
	*light_pct = (double)(BIT_MASK(LIGHT_ADC_RES) - adc_sample) * 100.0 /
		     (double)BIT_MASK(LIGHT_ADC_RES);
	return 0;
}

/* --- IOTM model blob: format, validation, inference ------------------------ */

#define MODEL_MAGIC    "IOTM"
#define MODEL_FMT_VER  1
#define MODEL_MAX_IN   4
#define MODEL_MAX_HID  16
#define MODEL_MAX_OUT  4
#define MODEL_HDR_LEN  64
#define MODEL_MAX_BLOB 768        /* header + worst-case 4-16-4 weights */
#define MODEL_B64_MAX  1100       /* base64 text for MODEL_MAX_BLOB */

struct model_hdr {
	char magic[4];
	uint16_t fmt_ver;
	uint16_t model_ver;
	uint8_t n_in;
	uint8_t n_hid;
	uint8_t n_out;
	uint8_t led_mask;             /* bit i: LED on while class i active */
	char labels[MODEL_MAX_OUT][12];
	uint32_t crc;                 /* IEEE CRC32 over the weights section */
} __packed;

BUILD_ASSERT(sizeof(struct model_hdr) == MODEL_HDR_LEN, "IOTM header is 64 B");

static K_MUTEX_DEFINE(model_lock);
static uint8_t model_blob[MODEL_MAX_BLOB] __aligned(4);
static size_t model_len;
static char model_src[8] = "none";   /* builtin / flash / cloud */

static size_t model_weights_len(const struct model_hdr *h)
{
	return sizeof(float) * ((size_t)h->n_hid * h->n_in + h->n_hid +
				(size_t)h->n_out * h->n_hid + h->n_out);
}

/* NULL when valid, else a short reason (also used in command ACKs). */
static const char *model_validate(const uint8_t *blob, size_t len)
{
	const struct model_hdr *h = (const struct model_hdr *)blob;

	if (len < MODEL_HDR_LEN) {
		return "blob shorter than header";
	}
	if (memcmp(h->magic, MODEL_MAGIC, 4) != 0) {
		return "bad magic (want IOTM)";
	}
	if (h->fmt_ver != MODEL_FMT_VER) {
		return "unsupported format version";
	}
	if (h->n_in < 1 || h->n_in > MODEL_MAX_IN ||
	    h->n_hid < 1 || h->n_hid > MODEL_MAX_HID ||
	    h->n_out < 1 || h->n_out > MODEL_MAX_OUT) {
		return "dims out of range";
	}
	if (len != MODEL_HDR_LEN + model_weights_len(h)) {
		return "length does not match dims";
	}
	if (crc32_ieee(blob + MODEL_HDR_LEN, len - MODEL_HDR_LEN) != h->crc) {
		return "weights CRC mismatch";
	}
	return NULL;
}

static int model_install(const uint8_t *blob, size_t len, const char *src,
			 bool persist)
{
	const char *err = model_validate(blob, len);
	const struct model_hdr *h = (const struct model_hdr *)blob;

	if (err != NULL) {
		LOG_WRN("model rejected: %s", err);
		return -EINVAL;
	}
	k_mutex_lock(&model_lock, K_FOREVER);
	memcpy(model_blob, blob, len);
	model_len = len;
	snprintf(model_src, sizeof(model_src), "%s", src);
	k_mutex_unlock(&model_lock);
	LOG_INF("model v%u active (%s, %u B, %u-%u-%u, classes: %s/%s/%s%s%s)",
		h->model_ver, src, (unsigned)len, h->n_in, h->n_hid, h->n_out,
		h->labels[0], h->labels[1], h->n_out > 2 ? h->labels[2] : "-",
		h->n_out > 3 ? "/" : "", h->n_out > 3 ? h->labels[3] : "");
	if (persist) {
		int ret = settings_save_one("mlmdl/blob", blob, len);

		if (ret) {
			LOG_WRN("model persist failed (%d)", ret);
		}
	}
	return 0;
}

/* Inference: x[n_in] -> ReLU(W1 x + b1) -> W2 h + b2 -> argmax.
 * Returns class index; *score = softmax probability of the winner. */
static int model_infer(const double x[], double *score, uint32_t *infer_us)
{
	float h[MODEL_MAX_HID], o[MODEL_MAX_OUT];
	uint32_t t0 = k_cycle_get_32();
	int best = 0;

	k_mutex_lock(&model_lock, K_FOREVER);
	const struct model_hdr *hd = (const struct model_hdr *)model_blob;
	const float *w = (const float *)(model_blob + MODEL_HDR_LEN);
	const float *w1 = w;
	const float *b1 = w1 + (size_t)hd->n_hid * hd->n_in;
	const float *w2 = b1 + hd->n_hid;
	const float *b2 = w2 + (size_t)hd->n_out * hd->n_hid;

	for (int i = 0; i < hd->n_hid; i++) {
		float a = b1[i];

		for (int j = 0; j < hd->n_in; j++) {
			a += w1[i * hd->n_in + j] * (float)x[j];
		}
		h[i] = (a > 0.0f) ? a : 0.0f;
	}
	for (int i = 0; i < hd->n_out; i++) {
		float a = b2[i];

		for (int j = 0; j < hd->n_hid; j++) {
			a += w2[i * hd->n_hid + j] * h[j];
		}
		o[i] = a;
		if (a > o[best]) {
			best = i;
		}
	}
	/* Max-subtracted softmax of the winning class. */
	float denom = 0.0f;

	for (int i = 0; i < hd->n_out; i++) {
		denom += expf(o[i] - o[best]);
	}
	*score = (denom > 0.0f) ? 1.0 / (double)denom : 0.0;
	k_mutex_unlock(&model_lock);
	*infer_us = k_cyc_to_us_floor32(k_cycle_get_32() - t0);
	return best;
}

/* --- Model persistence (NVS settings, key mlmdl/blob) ----------------------- */

static int mlmdl_set(const char *name, size_t len, settings_read_cb read_cb,
		     void *cb_arg)
{
	static uint8_t restore[MODEL_MAX_BLOB] __aligned(4);

	if (strcmp(name, "blob") != 0 || len > sizeof(restore)) {
		return -ENOENT;
	}
	if (read_cb(cb_arg, restore, len) != (ssize_t)len) {
		return -EIO;
	}
	/* Install without re-persisting (we are reading FROM flash). */
	(void)model_install(restore, len, "flash", false);
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(mlmdl, "mlmdl", NULL, mlmdl_set, NULL, NULL);

/* --- Chunked model transfer state ------------------------------------------ */

static K_MUTEX_DEFINE(xfer_lock);
static char xfer_b64[MODEL_B64_MAX];
static size_t xfer_len;
static bool xfer_open;

/* --- App state -------------------------------------------------------------- */

enum led_mode { LED_AUTO, LED_ON, LED_OFF };

static volatile enum led_mode led_mode = LED_AUTO;
static volatile int publish_interval_sec = 5;
static int led_state;

static double last_temp_c, last_light_pct;   /* last good sensor readings */
static bool temp_ok, light_ok;

static void led_apply(int on)
{
	led_state = on ? 1 : 0;
	gpio_pin_set_dt(&io1_led, led_state);
}

/* Sample sensors, run the active model, drive the LED. Returns class idx. */
static int sense_and_infer(double *score, uint32_t *infer_us,
			   char *label, size_t label_len)
{
	double v;

	if (read_temp_c(&v) == 0) {
		last_temp_c = v;
		temp_ok = true;
	}
	if (read_light_pct(&v) == 0) {
		last_light_pct = v;
		light_ok = true;
	}

	/* Feature scaling must match tools/build_model.py. */
	const double x[2] = { last_temp_c / 50.0, last_light_pct / 100.0 };
	int cls = model_infer(x, score, infer_us);

	k_mutex_lock(&model_lock, K_FOREVER);
	const struct model_hdr *h = (const struct model_hdr *)model_blob;

	snprintf(label, label_len, "%s", h->labels[cls]);
	if (led_mode == LED_AUTO) {
		led_apply((h->led_mask >> cls) & 1);
	}
	k_mutex_unlock(&model_lock);
	return cls;
}

static void publish_ml(void)
{
	char label[13];
	double score;
	uint32_t infer_us;

	(void)sense_and_infer(&score, &infer_us, label, sizeof(label));

	IotclMessageHandle msg = iotcl_telemetry_create();

	if (msg == NULL) {
		return;
	}
	if (temp_ok) {
		iotcl_telemetry_set_number(msg, "io1.temperature_c", last_temp_c);
	}
	if (light_ok) {
		iotcl_telemetry_set_number(msg, "io1.light_pct", last_light_pct);
	}

	k_mutex_lock(&model_lock, K_FOREVER);
	const struct model_hdr *h = (const struct model_hdr *)model_blob;

	iotcl_telemetry_set_number(msg, "ml.model_ver", (double)h->model_ver);
	k_mutex_unlock(&model_lock);
	iotcl_telemetry_set_string(msg, "ml.class", label);
	iotcl_telemetry_set_number(msg, "ml.score", score);
	iotcl_telemetry_set_string(msg, "ml.model_src", model_src);
	iotcl_telemetry_set_number(msg, "ml.infer_us", (double)infer_us);
	iotcl_telemetry_set_number(msg, "led", (double)led_state);
#if defined(CONFIG_IOTCONNECT_DEVICE_VITALS)
	iotc_vitals_append(msg);
#endif
	(void)iotcl_mqtt_send_telemetry(msg, false);
	iotcl_telemetry_destroy(msg);
	LOG_INF("ml: class=%s temp=%.1fC light=%.0f%% model=v%u(%s) %uus",
		label, last_temp_c, last_light_pct,
		((struct model_hdr *)model_blob)->model_ver, model_src,
		(unsigned)infer_us);
}

/* --- C2D command handling ---------------------------------------------------
 * Commands arrive as one string: "<name> [arg]". Match on the first token so
 * e.g. "model-reset" is not mistaken for "reset".
 */

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

static const char *install_b64(const char *b64, size_t b64_len)
{
	static uint8_t decoded[MODEL_MAX_BLOB] __aligned(4);
	size_t olen = 0;

	if (base64_decode(decoded, sizeof(decoded), &olen,
			  (const uint8_t *)b64, b64_len) != 0) {
		return "base64 decode failed";
	}
	const char *err = model_validate(decoded, olen);

	if (err != NULL) {
		return err;
	}
	(void)model_install(decoded, olen, "cloud", true);
	return NULL;
}

static void on_command(IotclC2dEventData data)
{
	const char *cmd = iotcl_c2d_get_command(data);
	const char *ack = iotcl_c2d_get_ack_id(data);
	int status = IOTCL_C2D_EVT_CMD_FAILED;
	char note[96] = "unknown command";

	LOG_INF("C2D command: %.48s%s", cmd ? cmd : "(null)",
		(cmd && strlen(cmd) > 48) ? "..." : "");
	if (cmd == NULL) {
		goto out;
	}

	/* Split "<name> [arg]". */
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

	if (tok_is(cmd, name_len, "model-push") && arg != NULL) {
		const char *err = install_b64(arg, strlen(arg));

		if (err == NULL) {
			status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
			snprintf(note, sizeof(note), "model v%u installed (%s)",
				 ((struct model_hdr *)model_blob)->model_ver,
				 model_src);
		} else {
			snprintf(note, sizeof(note), "rejected: %s", err);
		}
	} else if (tok_is(cmd, name_len, "model-begin")) {
		k_mutex_lock(&xfer_lock, K_FOREVER);
		xfer_len = 0;
		xfer_open = true;
		k_mutex_unlock(&xfer_lock);
		status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
		snprintf(note, sizeof(note), "transfer open (max %u b64 chars)",
			 (unsigned)sizeof(xfer_b64));
	} else if (tok_is(cmd, name_len, "model-data") && arg != NULL) {
		size_t n = strlen(arg);

		k_mutex_lock(&xfer_lock, K_FOREVER);
		if (!xfer_open) {
			snprintf(note, sizeof(note), "no transfer open");
		} else if (xfer_len + n > sizeof(xfer_b64)) {
			xfer_open = false;
			snprintf(note, sizeof(note), "overflow; transfer aborted");
		} else {
			memcpy(xfer_b64 + xfer_len, arg, n);
			xfer_len += n;
			status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
			snprintf(note, sizeof(note), "%u/%u b64 chars",
				 (unsigned)xfer_len, (unsigned)sizeof(xfer_b64));
		}
		k_mutex_unlock(&xfer_lock);
	} else if (tok_is(cmd, name_len, "model-commit")) {
		k_mutex_lock(&xfer_lock, K_FOREVER);
		if (!xfer_open || xfer_len == 0) {
			snprintf(note, sizeof(note), "no transfer open");
		} else {
			const char *err = install_b64(xfer_b64, xfer_len);

			xfer_open = false;
			if (err == NULL) {
				status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
				snprintf(note, sizeof(note),
					 "model v%u installed (chunked)",
					 ((struct model_hdr *)model_blob)->model_ver);
			} else {
				snprintf(note, sizeof(note), "rejected: %s", err);
			}
		}
		k_mutex_unlock(&xfer_lock);
	} else if (tok_is(cmd, name_len, "model-info")) {
		k_mutex_lock(&model_lock, K_FOREVER);
		const struct model_hdr *h = (const struct model_hdr *)model_blob;

		snprintf(note, sizeof(note),
			 "v%u %s %uB %u-%u-%u crc=%08x [%s/%s/%s]",
			 h->model_ver, model_src, (unsigned)model_len,
			 h->n_in, h->n_hid, h->n_out, (unsigned)h->crc,
			 h->labels[0], h->labels[1],
			 h->n_out > 2 ? h->labels[2] : "-");
		k_mutex_unlock(&model_lock);
		status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
	} else if (tok_is(cmd, name_len, "model-reset")) {
		(void)settings_delete("mlmdl/blob");
		(void)model_install(model_builtin, sizeof(model_builtin),
				    "builtin", false);
		status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
		snprintf(note, sizeof(note), "reverted to builtin model v%u",
			 ((struct model_hdr *)model_blob)->model_ver);
	} else if (tok_is(cmd, name_len, "led-on")) {
		led_mode = LED_ON;
		led_apply(1);
		status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
		snprintf(note, sizeof(note), "LED forced on");
	} else if (tok_is(cmd, name_len, "led-off")) {
		led_mode = LED_OFF;
		led_apply(0);
		status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
		snprintf(note, sizeof(note), "LED forced off");
	} else if (tok_is(cmd, name_len, "led-auto")) {
		led_mode = LED_AUTO;
		status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
		snprintf(note, sizeof(note), "LED follows the model");
	} else if (tok_is(cmd, name_len, "interval") && arg != NULL) {
		int v = atoi(arg);

		if (v >= 1 && v <= 3600) {
			publish_interval_sec = v;
			status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
			snprintf(note, sizeof(note), "interval=%ds", v);
		} else {
			snprintf(note, sizeof(note), "interval out of range");
		}
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

/* Native IOTCONNECT "AI Model" push (Devices -> AI Models -> Push Model).
 * The platform delivers a module command (ct:2) carrying a download URL --
 * the same schema as OTA, so it arrives on the OTA callback. Fetch the raw
 * IOTM blob over HTTPS, then validate + hot-swap + persist exactly like a
 * model-push command. The uploaded platform file is the raw .bin blob. */
static void on_model_push(IotclC2dEventData data)
{
	static uint8_t dl_buf[MODEL_MAX_BLOB] __aligned(4);
	const char *ack = iotcl_c2d_get_ack_id(data);
	const char *host = iotcl_c2d_get_ota_url_hostname(data, 0);
	const char *res = iotcl_c2d_get_ota_url_resource(data, 0);
	int status = IOTCL_C2D_EVT_OTA_DOWNLOAD_FAILED;
	char note[96];
	size_t len = 0;

	LOG_INF("Model push from platform: host=%s", host ? host : "(none)");
	if (host == NULL || res == NULL) {
		snprintf(note, sizeof(note), "no download URL in model push");
	} else {
		int ret = iotc_https_download(host, res,
					      CONFIG_IOTCONNECT_SEC_TAG_BROKER_CA,
					      CONFIG_IOTCONNECT_DRA_HTTP_TIMEOUT_MS,
					      dl_buf, sizeof(dl_buf), &len);

		if (ret != 0) {
			snprintf(note, sizeof(note), "download failed (%d)", ret);
		} else {
			const char *err = model_validate(dl_buf, len);

			if (err != NULL) {
				snprintf(note, sizeof(note), "rejected: %s", err);
			} else {
				(void)model_install(dl_buf, len, "cloud", true);
				status = IOTCL_C2D_EVT_OTA_DOWNLOAD_DONE;
				snprintf(note, sizeof(note),
					 "model v%u deployed (%u B)",
					 ((struct model_hdr *)model_blob)->model_ver,
					 (unsigned)len);
			}
		}
	}
	LOG_INF("Model push: %s", note);
	if (ack != NULL) {
		(void)iotcl_mqtt_send_ota_ack(ack, status, note);
	}
}

static void on_connection_status(IotConnectMqttStatus status)
{
	LOG_INF("MQTT status: %d", (int)status);
}

/* --- Network bring-up -------------------------------------------------------- */

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

int main(void)
{
	int ret;

	LOG_INF("ml-model-update demo starting (IOTM engine, fmt v%d)",
		MODEL_FMT_VER);
	if (io1_init() != 0) {
		return 0;
	}

	/* A cloud-pushed model persisted in NVS wins over the builtin. */
	(void)settings_subsys_init();
	(void)settings_load_subtree("mlmdl");
	if (model_len == 0) {
		(void)model_install(model_builtin, sizeof(model_builtin),
				    "builtin", false);
	}

	/* Bench self-test before touching the network: prove the IO1 wiring
	 * and the model end-to-end on the console. */
	{
		char lbl[13];
		double s;
		uint32_t us;

		(void)sense_and_infer(&s, &us, lbl, sizeof(lbl));
		LOG_INF("IO1 self-test: temp=%s%.2fC light=%s%.1f%% -> "
			"class=%s (%uus)",
			temp_ok ? "" : "(fail)", last_temp_c,
			light_ok ? "" : "(fail)", last_light_pct,
			lbl, (unsigned)us);
	}

	/* Identity is provisioned at the prompt: iotcprov provision <duid>, then
	 * register the printed cert and paste iotcDeviceConfig.json (iotc config). */
	struct iotc_identity id;

	if (iotc_identity_load(&id) != 0) {
		print_guide("no identity stored in NVS");
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
	config.auth_info.ca_cert = broker_ca_pem;      /* public roots, compiled in */
	config.auth_info.ca_cert_len = sizeof(broker_ca_pem);
	config.auth_info.dra_ca = dra_ca_pem;
	config.auth_info.dra_ca_len = sizeof(dra_ca_pem);
	config.auth_info.data.cert_info.device_cert = id.device_cert;   /* from NVS */
	config.auth_info.data.cert_info.device_cert_len = id.device_cert_len;
	config.auth_info.data.cert_info.device_key = id.device_key;     /* from NVS */
	config.auth_info.data.cert_info.device_key_len = id.device_key_len;

	config.status_cb = on_connection_status;
	config.cmd_cb = on_command;
	config.ota_cb = on_model_push;   /* native AI Model push (ct:2) */
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
		LOG_INF("Connected. Try: model-info, then push "
			"models/model_v2_comfort.cmd to swap behavior.");
		int since_publish = publish_interval_sec; /* publish at once */

		while (iotconnect_sdk_is_connected()) {
			if (since_publish >= publish_interval_sec) {
				publish_ml();
				since_publish = 0;
			} else {
				/* Keep the LED live between publishes. */
				char lbl[13];
				double s;
				uint32_t us;

				(void)sense_and_infer(&s, &us, lbl, sizeof(lbl));
			}
			k_sleep(K_SECONDS(1));
			since_publish++;
		}
		iotconnect_sdk_disconnect();
		LOG_WRN("Disconnected; reconnecting...");
	}
	return 0;
}
