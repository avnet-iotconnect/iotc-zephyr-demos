/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * vision-occupancy demo -- edge person detection on the NXP MIMXRT1170-EVKB
 * over Avnet /IOTCONNECT.
 *
 * The OV5640 camera (shipped with the EVKB, MIPI CSI-2) streams QVGA frames;
 * a TFLite-Micro person-detection CNN (int8 MobileNet, 96x96 grayscale,
 * CMSIS-NN kernels) classifies each frame ON the device. Only inferences
 * leave the board: the cloud sees vision.* telemetry (person score, occupancy
 * state, fps, latency), never video. An occupancy state machine with
 * hysteresis drives the board LED and flips OCCUPIED/CLEAR on the dashboard.
 *
 * Cloud interactions:
 *   snapshot            capture ONE frame, encode it as a grayscale PNG on
 *                       device, and upload it to the platform's Telemetry
 *                       Files panel (S3 + fu announce), tagged with the
 *                       live occupancy verdict as its Classification
 *   threshold <pct>     occupancy trigger threshold (person score, %)
 *   interval <sec>      telemetry publish interval
 *   led-on|led-off|led-auto
 *   model-info          ACK with the active model's identity
 *   model-fetch <url>   pull a new IOTV/.tflite model over HTTPS
 *   model-reset         revert to the built-in model (erases the flash copy)
 *   reboot
 *
 * THE MODEL IS DATA, NOT FIRMWARE: the platform's native AI-Model push (an
 * OTA-schema command with a download URL) or model-fetch delivers a new
 * ~300 KB .tflite in an IOTV envelope (magic/version/CRC32 -- the
 * ml-model-update demo's IOTM discipline, scaled up). It is validated, trial-
 * loaded with rollback, hot-swapped without reboot, and persisted to the
 * unused slot1 flash partition so it survives power cycles.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/gpio.h>
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

#include "vision_camera.h"
#include "vision_infer.h"
#include "model_store.h"
#include "iotc_file_upload.h"
#include "png_gray.h"

LOG_MODULE_REGISTER(vision_occupancy, LOG_LEVEL_INF);

/* Built-in model: the stock TFLM person-detect int8 .tflite, embedded from
 * the tflite-micro module at build time (see CMakeLists.txt). */
static const uint8_t model_builtin_tflite[] __aligned(16) = {
#include <person_detect_tflite.inc>
};

static void print_guide(const char *reason)
{
	printk("\n");
	printk("========================================================\n");
	printk("  VISION OCCUPANCY -- device is not provisioned\n");
	printk("  (%s)\n", reason);
	printk("--------------------------------------------------------\n");
	printk("  1) Generate this device's identity ON the device:\n");
	printk("        iotcprov provision <your-duid>\n");
	printk("  2) In IOTCONNECT: import the vision-occupancy template,\n");
	printk("     Create Device (Self-Signed) on it, and paste the\n");
	printk("     certificate printed above.\n");
	printk("  3) Paste the downloaded iotcDeviceConfig.json:\n");
	printk("        iotc config\n");
	printk("        { ...paste the json block... }\n");
	printk("  4) Connect:  kernel reboot cold\n");
	printk("========================================================\n\n");
}

/* --- Active model ----------------------------------------------------------
 * model_active holds the ACTIVE IOTV blob (header + .tflite); TFLM references
 * the payload in place, so it stays resident. model_staging receives
 * downloads/candidates; a candidate is trial-loaded from staging first, so a
 * bad push rolls back to the still-intact active copy. Both live in SDRAM.
 */

static K_MUTEX_DEFINE(model_lock);
static uint8_t model_active[MODEL_MAX_BLOB] __aligned(16);
static size_t model_active_len;
static uint8_t model_staging[MODEL_MAX_BLOB] __aligned(16);
static char model_src[8] = "none";   /* builtin / flash / cloud */

static const struct iotv_hdr *active_hdr(void)
{
	return (const struct iotv_hdr *)model_active;
}

/*
 * Install the IOTV blob sitting in model_staging (len bytes): validate the
 * envelope, trial-load the interpreter from staging (rolling back to the
 * active model on failure), then commit to model_active (+ optionally the
 * flash store). Serialized by model_lock. Fills note for command ACKs.
 */
static int install_from_staging(size_t len, const char *src, bool persist,
				char *note, size_t note_len)
{
	char why[64] = "";
	const char *err = iotv_validate(model_staging, len);
	const struct iotv_hdr *h = (const struct iotv_hdr *)model_staging;
	int ret = -EINVAL;

	if (err != NULL) {
		snprintf(note, note_len, "rejected: %s", err);
		return -EINVAL;
	}

	k_mutex_lock(&model_lock, K_FOREVER);
	if (vision_infer_init(model_staging + IOTV_HDR_LEN, h->model_len,
			      why, sizeof(why)) != 0) {
		snprintf(note, note_len, "rejected: %s", why);
		/* Roll back to the intact active model. */
		if (model_active_len > 0) {
			(void)vision_infer_init(model_active + IOTV_HDR_LEN,
						active_hdr()->model_len,
						NULL, 0);
		}
		goto out;
	}

	memcpy(model_active, model_staging, len);
	model_active_len = len;
	snprintf(model_src, sizeof(model_src), "%s", src);
	/* Same bytes that just passed the trial load; cannot fail. */
	(void)vision_infer_init(model_active + IOTV_HDR_LEN,
				active_hdr()->model_len, NULL, 0);
	ret = 0;
	LOG_INF("model v%u \"%.15s\" active (%s, %u B, arena %u B)",
		active_hdr()->model_ver, active_hdr()->name, model_src,
		(unsigned int)len, (unsigned int)vision_infer_arena_used());
	snprintf(note, note_len, "model v%u \"%.15s\" active (%s, %u B)",
		 active_hdr()->model_ver, active_hdr()->name, model_src,
		 (unsigned int)len);

out:
	k_mutex_unlock(&model_lock);
	if (ret == 0 && persist) {
		if (model_store_save(model_active, model_active_len) != 0) {
			LOG_WRN("model not persisted; still active until reboot");
		}
	}
	return ret;
}

/* Stage the built-in model and install it (also boot fallback). */
static int install_builtin(char *note, size_t note_len)
{
	memcpy(model_staging + IOTV_HDR_LEN, model_builtin_tflite,
	       sizeof(model_builtin_tflite));
	iotv_wrap_in_place(model_staging, sizeof(model_builtin_tflite), 1,
			   "person-builtin");
	return install_from_staging(IOTV_HDR_LEN + sizeof(model_builtin_tflite),
				    "builtin", false, note, note_len);
}

/* --- Model delivery (platform AI-Model push / model-fetch) -----------------
 * Downloads run in a dedicated worker thread -- a ~300 KB TLS download plus
 * a multi-second flash erase must not stall the MQTT message pump. The
 * worker never touches MQTT: results are queued back and ACKed from the
 * main loop.
 */

struct model_job {
	char host[128];
	/* Presigned S3 URLs run ~1.5 KB (the security token dominates); a
	 * 1024-byte field truncated the query mid-token and S3 answered
	 * "No AWSAccessKey was presented" (hardware-observed). */
	char res[1664];
	char ack[80];
	bool is_ota;                 /* ACK on the OTA schema vs command */
};

struct model_result {
	char ack[80];
	char note[128];
	bool is_ota;
	bool ok;
};

K_MSGQ_DEFINE(model_jobq, sizeof(struct model_job), 2, 4);
K_MSGQ_DEFINE(model_resq, sizeof(struct model_result), 2, 4);

/*
 * Minimal ZIP walk (from the ml-model-update demo): the platform's AI Models
 * upload takes a .zip and may repackage it, so scan local-file entries for a
 * STORED (uncompressed) payload. Accepts an entry that is either an IOTV
 * blob or a raw .tflite flatbuffer. Points at the payload in place.
 */
#define ZIP_LOCAL_HDR_LEN 30

static bool entry_is_model(const uint8_t *p, size_t len)
{
	return (len >= IOTV_HDR_LEN && memcmp(p, IOTV_MAGIC, 4) == 0) ||
	       (len >= 8 && memcmp(p + 4, "TFL3", 4) == 0);
}

static const char *zip_find_model_stored(const uint8_t *buf, size_t len,
					 const uint8_t **payload,
					 size_t *payload_len)
{
	bool compressed_seen = false;
	size_t off = 0;

	if (len < ZIP_LOCAL_HDR_LEN || memcmp(buf, "PK\x03\x04", 4) != 0) {
		return "not a zip archive";
	}
	while (off + ZIP_LOCAL_HDR_LEN <= len &&
	       memcmp(buf + off, "PK\x03\x04", 4) == 0) {
		uint16_t flags = sys_get_le16(buf + off + 6);
		uint16_t method = sys_get_le16(buf + off + 8);
		uint32_t comp_size = sys_get_le32(buf + off + 18);
		size_t data_off = off + ZIP_LOCAL_HDR_LEN +
				  sys_get_le16(buf + off + 26) +
				  sys_get_le16(buf + off + 28);

		if (flags & 0x08) {
			return "zip uses a streaming data descriptor";
		}
		if (data_off + comp_size > len) {
			return "zip entry truncated";
		}
		if (method == 0 && !(flags & 0x01) &&
		    entry_is_model(buf + data_off, comp_size)) {
			*payload = buf + data_off;
			*payload_len = comp_size;
			return NULL;
		}
		if (method != 0) {
			compressed_seen = true;
		}
		off = data_off + comp_size;
	}
	return compressed_seen ?
		"no stored model entry (zip is compressed; use 'store')" :
		"no model entry found in zip";
}

/*
 * Normalize whatever arrived in model_staging (zip / IOTV / raw .tflite,
 * body_len bytes) into an IOTV blob at the start of model_staging.
 * Returns NULL and sets *out_len on success, else a short reason.
 */
static const char *normalize_staged_download(size_t body_len, size_t *out_len)
{
	const uint8_t *payload = model_staging;
	size_t payload_len = body_len;

	if (body_len >= 4 && memcmp(model_staging, "PK", 2) == 0) {
		const char *err = zip_find_model_stored(model_staging, body_len,
							&payload, &payload_len);
		if (err != NULL) {
			return err;
		}
	}

	if (payload_len >= IOTV_HDR_LEN &&
	    memcmp(payload, IOTV_MAGIC, 4) == 0) {
		memmove(model_staging, payload, payload_len);
		*out_len = payload_len;
		return NULL;
	}
	if (payload_len >= 8 && memcmp(payload + 4, "TFL3", 4) == 0) {
		if (payload_len > MODEL_MAX_BLOB - IOTV_HDR_LEN) {
			return "model too large";
		}
		/* Raw flatbuffer: make room for a header and wrap it. */
		memmove(model_staging + IOTV_HDR_LEN, payload, payload_len);
		iotv_wrap_in_place(model_staging, payload_len, 0, "unversioned");
		*out_len = IOTV_HDR_LEN + payload_len;
		return NULL;
	}
	return "body is neither zip, IOTV, nor .tflite";
}

static void model_worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	struct model_job job;
	struct model_result res;

	while (true) {
		k_msgq_get(&model_jobq, &job, K_FOREVER);
		memset(&res, 0, sizeof(res));
		strcpy(res.ack, job.ack);
		res.is_ota = job.is_ota;
		res.ok = false;

		LOG_INF("model download: https://%s%.60s...", job.host, job.res);

		size_t body_len = 0;
		int ret = iotc_https_download(job.host, job.res,
					      CONFIG_IOTCONNECT_SEC_TAG_BROKER_CA,
					      CONFIG_IOTCONNECT_DRA_HTTP_TIMEOUT_MS,
					      model_staging, MODEL_MAX_BLOB,
					      &body_len);

		if (ret != 0) {
			snprintf(res.note, sizeof(res.note),
				 "download failed (%d)", ret);
		} else {
			size_t blob_len = 0;
			const char *err = normalize_staged_download(body_len,
								    &blob_len);

			LOG_INF("downloaded %u B", (unsigned int)body_len);
			if (err == NULL) {
				res.ok = install_from_staging(blob_len, "cloud",
							      true, res.note,
							      sizeof(res.note)) == 0;
			} else {
				snprintf(res.note, sizeof(res.note),
					 "rejected: %s", err);
				if (body_len > 0 && model_staging[0] == '<') {
					/* S3/HTTP error XML: log as text --
					 * it names the exact failure.
					 * Newlines would truncate the line. */
					size_t xn = MIN(body_len, 400);

					for (size_t x = 0; x < xn; x++) {
						if (model_staging[x] == '\n' ||
						    model_staging[x] == '\r') {
							model_staging[x] = ' ';
						}
					}
					model_staging[xn] = '\0';
					LOG_WRN("server said: %s",
						(char *)model_staging);
				} else {
					LOG_HEXDUMP_INF(model_staging,
							MIN(body_len, 64),
							"body head:");
				}
			}
		}

		LOG_INF("model push: %s", res.note);
		(void)k_msgq_put(&model_resq, &res, K_NO_WAIT);
	}
}

K_THREAD_DEFINE(model_worker_tid, 16384, model_worker, NULL, NULL, NULL, 7, 0, 0);

/* Queue a download job. src strings are copied. Returns 0 or -EBUSY. */
static int queue_model_job(const char *host, const char *res, const char *ack,
			   bool is_ota)
{
	struct model_job job;

	memset(&job, 0, sizeof(job));
	/* A truncated URL must never go out (S3 rejects it confusingly);
	 * refuse loudly instead. */
	if (strlen(host) >= sizeof(job.host) ||
	    strlen(res) >= sizeof(job.res)) {
		LOG_ERR("model URL too long (host %u, res %u)",
			(unsigned int)strlen(host), (unsigned int)strlen(res));
		return -EINVAL;
	}
	snprintf(job.host, sizeof(job.host), "%s", host);
	snprintf(job.res, sizeof(job.res), "%s", res);
	if (ack != NULL) {
		snprintf(job.ack, sizeof(job.ack), "%s", ack);
	}
	job.is_ota = is_ota;
	return k_msgq_put(&model_jobq, &job, K_NO_WAIT) == 0 ? 0 : -EBUSY;
}

/* --- App state -------------------------------------------------------------- */

enum led_mode { LED_AUTO, LED_ON, LED_OFF };

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static volatile enum led_mode led_mode = LED_AUTO;
static int led_state;
static bool led_ok;

/* Occupancy state machine (hysteresis in consecutive frames). */
#define OCC_ENTER_FRAMES 2
#define OCC_CLEAR_FRAMES 5

static volatile int threshold_pct = 60;
static volatile int publish_interval_sec = 10;

static bool cam_ok;
static bool occupied;
static int consec_above, consec_below;
static int last_person_pct, last_clear_pct;
static uint32_t last_infer_ms;
static uint32_t total_frames;
static int fps_x10;                  /* measured inference rate, fps * 10 */

static void led_apply(int on)
{
	led_state = on ? 1 : 0;
	if (led_ok) {
		gpio_pin_set_dt(&led, led_state);
	}
}

/* --- Bench shell: see what the camera sees, no cloud needed ----------------
 * `vision status` prints the live pipeline numbers; `vision snap` dumps one
 * decimated grayscale frame as base64 between SNAP-BEGIN/SNAP-END markers
 * (decode on the host with tools/decode_console_snap.py).
 */
#include <zephyr/shell/shell.h>
#include <zephyr/sys/base64.h>

static int snapshot_request(void); /* defined with the vision loop below */

/* Last AI-Model push URL (bench debugging via `vision url`). */
static char last_model_url[1600];

static int cmd_vision_url(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (last_model_url[0] == '\0') {
		shell_print(sh, "(no model push received yet)");
		return 0;
	}
	shell_print(sh, "URL-BEGIN %u", (unsigned int)strlen(last_model_url));
	for (size_t off = 0; off < strlen(last_model_url); off += 96) {
		shell_print(sh, "%.96s", &last_model_url[off]);
	}
	shell_print(sh, "URL-END");
	return 0;
}

static int cmd_vision_upload(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int ret = snapshot_request();

	if (ret == 0) {
		shell_print(sh, "snapshot armed; uploading to Telemetry Files");
	} else {
		shell_error(sh, "snapshot_request: %d", ret);
	}
	return ret;
}

static int cmd_vision_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	shell_print(sh, "cam=%d state=%s person=%d%% clear=%d%% thr=%d%% "
		    "infer=%ums fps=%d.%d frames=%u",
		    cam_ok, occupied ? "occupied" : "clear",
		    last_person_pct, last_clear_pct, threshold_pct,
		    (unsigned int)last_infer_ms, fps_x10 / 10, fps_x10 % 10,
		    (unsigned int)total_frames);
	return 0;
}

static int cmd_vision_snap(const struct shell *sh, size_t argc, char **argv)
{
	static uint8_t gray[VISION_INPUT_W * VISION_INPUT_H];
	static uint8_t snap[SNAPSHOT_MAX_LEN];
	static char b64[((SNAPSHOT_MAX_LEN + 2) / 3) * 4 + 4];
	uint16_t w = 0, h = 0;
	size_t olen = 0;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	if (!cam_ok) {
		shell_error(sh, "camera not available");
		return -ENODEV;
	}
	if (vision_camera_capture(gray, snap, &w, &h, 2000) != 0) {
		shell_error(sh, "no frame");
		return -EIO;
	}
	if (base64_encode((uint8_t *)b64, sizeof(b64), &olen, snap,
			  (size_t)w * h) != 0) {
		shell_error(sh, "base64 failed");
		return -ENOMEM;
	}
	shell_print(sh, "SNAP-BEGIN %u %u %u", w, h, (unsigned int)olen);
	for (size_t off = 0; off < olen; off += 120) {
		shell_print(sh, "%.120s", &b64[off]);
	}
	shell_print(sh, "SNAP-END");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_vision,
	SHELL_CMD(status, NULL, "Vision pipeline status", cmd_vision_status),
	SHELL_CMD(snap, NULL, "Dump one frame as base64", cmd_vision_snap),
	SHELL_CMD(upload, NULL, "Upload a snapshot to Telemetry Files",
		  cmd_vision_upload),
	SHELL_CMD(url, NULL, "Print the last AI-Model push URL", cmd_vision_url),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(vision, &sub_vision, "Vision bench commands", NULL);

/* Feed one inference result into the occupancy state machine; returns true
 * when the OCCUPIED/CLEAR state flipped. */
static bool occupancy_update(int person_pct)
{
	bool flipped = false;

	if (person_pct >= threshold_pct) {
		consec_above++;
		consec_below = 0;
	} else {
		consec_below++;
		consec_above = 0;
	}

	if (!occupied && consec_above >= OCC_ENTER_FRAMES) {
		occupied = true;
		flipped = true;
		LOG_INF("OCCUPIED (score %d%% >= %d%%)", person_pct,
			threshold_pct);
	} else if (occupied && consec_below >= OCC_CLEAR_FRAMES) {
		occupied = false;
		flipped = true;
		LOG_INF("CLEAR (score %d%% < %d%%)", person_pct, threshold_pct);
	}

	if (led_mode == LED_AUTO) {
		led_apply(occupied);
	}
	return flipped;
}

static void publish_vision(void)
{
	IotclMessageHandle msg = iotcl_telemetry_create();

	if (msg == NULL) {
		return;
	}
	iotcl_telemetry_set_number(msg, "vision.person", occupied ? 1 : 0);
	iotcl_telemetry_set_string(msg, "vision.state",
				   occupied ? "occupied" : "clear");
	iotcl_telemetry_set_number(msg, "vision.score", (double)last_person_pct);
	iotcl_telemetry_set_number(msg, "vision.clear_score",
				   (double)last_clear_pct);
	iotcl_telemetry_set_number(msg, "vision.threshold",
				   (double)threshold_pct);
	iotcl_telemetry_set_number(msg, "vision.infer_ms", (double)last_infer_ms);
	iotcl_telemetry_set_number(msg, "vision.fps", (double)fps_x10 / 10.0);
	iotcl_telemetry_set_number(msg, "vision.frames", (double)total_frames);
	iotcl_telemetry_set_number(msg, "vision.cam_ok", cam_ok ? 1 : 0);

	k_mutex_lock(&model_lock, K_FOREVER);
	if (model_active_len > 0) {
		iotcl_telemetry_set_number(msg, "model.ver",
					   (double)active_hdr()->model_ver);
		iotcl_telemetry_set_string(msg, "model.name", active_hdr()->name);
		iotcl_telemetry_set_number(msg, "model.size_b",
					   (double)active_hdr()->model_len);
	}
	iotcl_telemetry_set_string(msg, "model.src", model_src);
	iotcl_telemetry_set_number(msg, "model.arena_b",
				   (double)vision_infer_arena_used());
	k_mutex_unlock(&model_lock);

	iotcl_telemetry_set_number(msg, "led", (double)led_state);
#if defined(CONFIG_IOTCONNECT_DEVICE_VITALS)
	iotc_vitals_append(msg);
#endif
	(void)iotcl_mqtt_send_telemetry(msg, false);
	iotcl_telemetry_destroy(msg);
}

/* --- C2D command handling --------------------------------------------------- */

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

/* Split "https://host/path?query" into host + resource. Returns 0 on success. */
static int parse_https_url(const char *url, char *host, size_t host_len,
			   char *res, size_t res_len)
{
	const char *p = url;
	const char *slash;

	if (strncmp(p, "https://", 8) != 0) {
		return -EINVAL;
	}
	p += 8;
	slash = strchr(p, '/');
	if (slash == NULL || slash == p ||
	    (size_t)(slash - p) >= host_len) {
		return -EINVAL;
	}
	memcpy(host, p, slash - p);
	host[slash - p] = '\0';
	if (strlen(slash) >= res_len) {
		return -EINVAL;
	}
	snprintf(res, res_len, "%s", slash);
	return 0;
}

static void on_command(IotclC2dEventData data)
{
	const char *cmd = iotcl_c2d_get_command(data);
	const char *ack = iotcl_c2d_get_ack_id(data);
	int status = IOTCL_C2D_EVT_CMD_FAILED;
	char note[128] = "unknown command";

	LOG_INF("C2D command: %.64s%s", cmd ? cmd : "(null)",
		(cmd && strlen(cmd) > 64) ? "..." : "");
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

	if (tok_is(cmd, name_len, "snapshot")) {
		int ret = snapshot_request();

		if (ret == 0) {
			status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
			snprintf(note, sizeof(note),
				 "snapshot uploading; see Telemetry Files");
		} else if (ret == -ENOTSUP) {
			snprintf(note, sizeof(note),
				 "file upload unavailable (File Support off?)");
		} else if (ret == -ENODEV) {
			snprintf(note, sizeof(note), "camera not available");
		} else {
			snprintf(note, sizeof(note), "snapshot busy");
		}
	} else if (tok_is(cmd, name_len, "threshold") && arg != NULL) {
		int v = atoi(arg);

		if (v >= 1 && v <= 99) {
			threshold_pct = v;
			status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
			snprintf(note, sizeof(note), "threshold=%d%%", v);
		} else {
			snprintf(note, sizeof(note), "threshold out of range");
		}
	} else if (tok_is(cmd, name_len, "interval") && arg != NULL) {
		int v = atoi(arg);

		if (v >= 1 && v <= 3600) {
			publish_interval_sec = v;
			status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
			snprintf(note, sizeof(note), "interval=%ds", v);
		} else {
			snprintf(note, sizeof(note), "interval out of range");
		}
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
		snprintf(note, sizeof(note), "LED follows occupancy");
	} else if (tok_is(cmd, name_len, "model-info")) {
		k_mutex_lock(&model_lock, K_FOREVER);
		if (model_active_len > 0) {
			snprintf(note, sizeof(note),
				 "v%u \"%.15s\" %s %uB crc=%08x arena=%uB",
				 active_hdr()->model_ver, active_hdr()->name,
				 model_src,
				 (unsigned int)active_hdr()->model_len,
				 (unsigned int)active_hdr()->crc,
				 (unsigned int)vision_infer_arena_used());
		} else {
			snprintf(note, sizeof(note), "no model loaded");
		}
		k_mutex_unlock(&model_lock);
		status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
	} else if (tok_is(cmd, name_len, "model-reset")) {
		(void)model_store_erase();
		if (install_builtin(note, sizeof(note)) == 0) {
			status = IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK;
		}
	} else if (tok_is(cmd, name_len, "model-fetch") && arg != NULL) {
		char host[128], res[1024];

		if (parse_https_url(arg, host, sizeof(host),
				    res, sizeof(res)) != 0) {
			snprintf(note, sizeof(note), "need https://host/path");
		} else if (queue_model_job(host, res, ack, false) != 0) {
			snprintf(note, sizeof(note), "model transfer busy");
		} else {
			/* ACK comes from the main loop when the job finishes. */
			return;
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

/* Native IOTCONNECT AI-Model push (Devices -> AI Models -> Push Model): an
 * OTA-schema command carrying a download URL. Queue it for the worker; the
 * OTA ACK is sent from the main loop when the install finishes. */
static void on_model_push(IotclC2dEventData data)
{
	const char *ack = iotcl_c2d_get_ack_id(data);
	const char *host = iotcl_c2d_get_ota_url_hostname(data, 0);
	const char *res = iotcl_c2d_get_ota_url_resource(data, 0);

	LOG_INF("model push from platform: host=%s", host ? host : "(none)");
	/* Stash the full URL for `vision url` (shell output cannot drop the
	 * way burst logging does) -- bench debugging aid. */
	if (res != NULL) {
		snprintf(last_model_url, sizeof(last_model_url),
			 "https://%s%s", host ? host : "", res);
	}
	if (host == NULL || res == NULL) {
		if (ack != NULL) {
			(void)iotcl_mqtt_send_ota_ack(
				ack, IOTCL_C2D_EVT_OTA_DOWNLOAD_FAILED,
				"no download URL in model push");
		}
		return;
	}
	if (queue_model_job(host, res, ack, true) != 0 && ack != NULL) {
		(void)iotcl_mqtt_send_ota_ack(ack,
					      IOTCL_C2D_EVT_OTA_DOWNLOAD_FAILED,
					      "model transfer busy");
	}
}

/* ACK finished model jobs from the main loop (single MQTT-sending thread). */
static void drain_model_results(void)
{
	struct model_result res;

	while (k_msgq_get(&model_resq, &res, K_NO_WAIT) == 0) {
		if (res.ack[0] == '\0') {
			continue;
		}
		if (res.is_ota) {
			(void)iotcl_mqtt_send_ota_ack(
				res.ack,
				res.ok ? IOTCL_C2D_EVT_OTA_DOWNLOAD_DONE
				       : IOTCL_C2D_EVT_OTA_DOWNLOAD_FAILED,
				res.note);
		} else {
			(void)iotcl_mqtt_send_cmd_ack(
				res.ack,
				res.ok ? IOTCL_C2D_EVT_CMD_SUCCESS_WITH_ACK
				       : IOTCL_C2D_EVT_CMD_FAILED,
				res.note);
		}
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

/* --- Capture + inference step ---------------------------------------------- */

static uint8_t gray96[VISION_INPUT_LEN];
static uint8_t snap_frame[SNAPSHOT_MAX_LEN];

/*
 * Snapshot -> Telemetry Files: the vision loop captures the frame and its
 * verdict; this worker does the slow part (PNG encode + credentials + S3
 * PUT + fu announce) off the loop and off the MQTT callback thread.
 */
static uint8_t png_buf[PNG_GRAY_MAX_SIZE(SNAPSHOT_MAX_W, SNAPSHOT_MAX_H)];
static uint16_t snap_w, snap_h;
static char snap_cf[128];
static volatile bool snap_want;   /* command asked for a frame */
static volatile bool snap_busy;   /* worker owns snap_frame/png_buf */
static K_SEM_DEFINE(snap_go, 0, 1);

static void snapshot_worker(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (true) {
		k_sem_take(&snap_go, K_FOREVER);

		char name[40];
		int n = png_gray_encode(snap_frame, snap_w, snap_h, png_buf,
					sizeof(png_buf));

		if (n > 0) {
			snprintf(name, sizeof(name), "snap-%u.png",
				 (unsigned int)time(NULL));
			int ret = iotc_fu_upload(name, png_buf, (size_t)n,
						 "image/png", snap_cf);

			if (ret == 0) {
				LOG_INF("snapshot %s (%d B) in Telemetry Files",
					name, n);
			} else {
				LOG_ERR("snapshot upload failed (%d)", ret);
			}
		} else {
			LOG_ERR("png encode failed (%d)", n);
		}
		snap_busy = false;
	}
}

K_THREAD_DEFINE(snap_worker_tid, 8192, snapshot_worker, NULL, NULL, NULL,
		K_PRIO_PREEMPT(9), 0, 0);

/* Arm a snapshot (from the C2D command or the bench shell). */
static int snapshot_request(void)
{
	if (!cam_ok) {
		return -ENODEV;
	}
	if (!iotc_fu_available()) {
		return -ENOTSUP;
	}
	if (snap_want || snap_busy) {
		return -EBUSY;
	}
	snap_want = true;
	return 0;
}

/* Capture one frame, run the model, update occupancy. Returns true when the
 * occupancy state flipped (publish immediately). */
static bool vision_step(void)
{
	bool want_snap = snap_want && !snap_busy;
	uint16_t sw = 0, sh = 0;
	int person = 0, clear = 0;
	bool flipped = false;

	if (!cam_ok) {
		return false;
	}
	if (vision_camera_capture(gray96, want_snap ? snap_frame : NULL,
				  &sw, &sh, 500) != 0) {
		return false;
	}

	k_mutex_lock(&model_lock, K_FOREVER);
	if (vision_infer_run(gray96, &person, &clear, &last_infer_ms) == 0) {
		k_mutex_unlock(&model_lock);
		last_person_pct = person;
		last_clear_pct = clear;
		total_frames++;
		flipped = occupancy_update(person);
	} else {
		k_mutex_unlock(&model_lock);
	}

	if (want_snap && sw > 0) {
		/* Hand the captured frame + its verdict to the upload worker. */
		snap_w = sw;
		snap_h = sh;
		k_mutex_lock(&model_lock, K_FOREVER);
		snprintf(snap_cf, sizeof(snap_cf),
			 "{\"state\":\"%s\",\"person_pct\":%d,\"model\":\"%.15s v%u\"}",
			 occupied ? "occupied" : "clear", person,
			 active_hdr()->name, active_hdr()->model_ver);
		k_mutex_unlock(&model_lock);
		snap_want = false;
		snap_busy = true;
		k_sem_give(&snap_go);
	}
	return flipped;
}

int main(void)
{
	int ret;
	char note[128];

	LOG_INF("vision-occupancy demo starting (IOTV fmt v%d)", IOTV_FMT_VER);

	led_ok = gpio_is_ready_dt(&led);
	if (led_ok) {
		gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	}

	/* Model: a cloud-pushed model persisted in flash wins over builtin. */
	size_t stored = 0;

	if (model_store_load(model_staging, MODEL_MAX_BLOB, &stored) == 0 &&
	    install_from_staging(stored, "flash", false, note,
				 sizeof(note)) == 0) {
		LOG_INF("restored persisted model: %s", note);
	} else if (install_builtin(note, sizeof(note)) != 0) {
		LOG_ERR("builtin model failed to load: %s", note);
		return 0;
	}

	/* Camera up + bench self-test BEFORE the network: prove the shield
	 * wiring and the model end-to-end on the console alone. */
	cam_ok = vision_camera_init() == 0;
	if (cam_ok) {
		int person = 0, clear = 0;

		/* First frame needs sensor warmup (AE + MIPI start): 5 s. */
		if (vision_camera_capture(gray96, NULL, NULL, NULL, 5000) == 0 &&
		    vision_infer_run(gray96, &person, &clear,
				     &last_infer_ms) == 0) {
			LOG_INF("self-test: person=%d%% clear=%d%% (%u ms, "
				"arena %u B)", person, clear,
				(unsigned int)last_infer_ms,
				(unsigned int)vision_infer_arena_used());
		} else {
			LOG_WRN("self-test capture/infer failed; stopping "
				"the camera stream");
			vision_camera_stop();
			cam_ok = false;
		}
	} else {
		LOG_WRN("camera not available -- check the OV5640 module on "
			"J2 and the --shield nxp_btb44_ov5640 build flag; "
			"continuing for provisioning/connectivity");
	}

	/* Identity is provisioned at the prompt: iotcprov provision <duid>,
	 * then register the printed cert + paste iotcDeviceConfig.json. */
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
	config.ota_cb = on_model_push;   /* native AI-Model push */
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
		LOG_INF("Connected. Try: model-info, snapshot, threshold 60.");

		int64_t last_pub = 0;   /* 0 -> publish immediately */
		int64_t win_start = k_uptime_get();
		uint32_t win_frames = 0;

		while (iotconnect_sdk_is_connected()) {
			bool flipped = vision_step();

			if (cam_ok) {
				win_frames++;
			}
			drain_model_results();

			int64_t now = k_uptime_get();

			if (flipped || last_pub == 0 ||
			    (now - last_pub) >= (int64_t)publish_interval_sec * 1000) {
				if (now > win_start) {
					fps_x10 = (int)((win_frames * 10000) /
							(uint32_t)(now - win_start));
				}
				win_start = now;
				win_frames = 0;
				publish_vision();
				last_pub = now;
			}
			k_sleep(K_MSEC(cam_ok ? 200 : 1000));
		}
		iotconnect_sdk_disconnect();
		LOG_WRN("Disconnected; reconnecting...");
	}
	return 0;
}
