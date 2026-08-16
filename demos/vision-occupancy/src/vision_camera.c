/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 */
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/video.h>
#include <zephyr/logging/log.h>

#include "vision_camera.h"

LOG_MODULE_REGISTER(vision_cam, LOG_LEVEL_INF);

#if !DT_HAS_CHOSEN(zephyr_camera)
#error "No camera in devicetree. Build with --shield nxp_btb44_ov5640 (RT1170-EVKB)."
#endif

#define CAPTURE_W 320
#define CAPTURE_H 240
/* 3 buffers: one held as the latest complete frame, two cycling in the
 * capture DMA. With only two, the pipeline starves the moment a frame is
 * held (or simply never dequeued) -- hardware-observed to wedge the whole
 * system after a few minutes. */
#define NUM_BUFS  3

static const struct device *const camera_dev =
	DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));

static struct video_format cam_fmt = {
	.type = VIDEO_BUF_TYPE_OUTPUT,
};

/*
 * Drain thread: continuously dequeues finished frames and re-enqueues the
 * previously held one, so the capture DMA always has buffers regardless of
 * how often (or whether) the app consumes frames. Consumers convert straight
 * out of `latest` under the lock.
 */
static K_MUTEX_DEFINE(latest_lock);
static struct video_buffer *latest;
static volatile bool cam_stopped;
static struct k_thread drain_thread;
static K_THREAD_STACK_DEFINE(drain_stack, 2048);

static void drain_fn(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (!cam_stopped) {
		struct video_buffer *vbuf = &(struct video_buffer){};
		struct video_buffer *old = NULL;

		vbuf->type = VIDEO_BUF_TYPE_OUTPUT;
		if (video_dequeue(camera_dev, &vbuf, K_MSEC(1000)) != 0) {
			continue;
		}
		k_mutex_lock(&latest_lock, K_FOREVER);
		old = latest;
		latest = vbuf;
		k_mutex_unlock(&latest_lock);
		if (old != NULL) {
			old->type = VIDEO_BUF_TYPE_OUTPUT;
			if (video_enqueue(camera_dev, old) != 0) {
				LOG_WRN("re-enqueue failed");
			}
		}
	}
}

/* --- pixel -> luma helpers -------------------------------------------------- */

/* RGB565 (little-endian in the buffer) to 8-bit luma: 0.30 R 0.59 G 0.11 B. */
static inline uint8_t rgb565_luma(const uint8_t *p)
{
	uint16_t v = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
	uint8_t r = (uint8_t)(((v >> 11) & 0x1F) << 3);
	uint8_t g = (uint8_t)(((v >> 5) & 0x3F) << 2);
	uint8_t b = (uint8_t)((v & 0x1F) << 3);

	return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
}

/* Luma of pixel x in a YUYV pair array: Y bytes sit at even offsets. */
static inline uint8_t yuyv_luma(const uint8_t *line, uint32_t x)
{
	return line[x * 2];
}

/* BGRX32 ("XR24", the RT1170 CSI pipeline default): bytes are B,G,R,X. */
static inline uint8_t bgrx32_luma(const uint8_t *p)
{
	return (uint8_t)((p[2] * 77 + p[1] * 150 + p[0] * 29) >> 8);
}

static inline uint8_t pix_luma(const uint8_t *line, uint32_t x)
{
	switch (cam_fmt.pixelformat) {
	case VIDEO_PIX_FMT_YUYV:
		return yuyv_luma(line, x);
	case VIDEO_PIX_FMT_BGRX32:
		return bgrx32_luma(line + x * 4);
	default:
		return rgb565_luma(line + x * 2);
	}
}

/* --- init ------------------------------------------------------------------- */

int vision_camera_init(void)
{
	struct video_caps caps = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	int ret;

	if (!device_is_ready(camera_dev)) {
		LOG_ERR("camera %s not ready", camera_dev->name);
		return -ENODEV;
	}

	ret = video_get_caps(camera_dev, &caps);
	if (ret < 0) {
		LOG_ERR("video_get_caps failed (%d)", ret);
		return ret;
	}

	ret = video_get_format(camera_dev, &cam_fmt);
	if (ret < 0) {
		LOG_ERR("video_get_format failed (%d)", ret);
		return ret;
	}

	/* Ask for QVGA RGB565; fall back to the pipeline's own default. Either
	 * way the format must be SET successfully -- a set_compose_format call
	 * is what programs the sensor + CSI chain coherently, and streaming
	 * without one leaves the capture DMA misconfigured (hardware-observed:
	 * no frames, then memory corruption). */
	cam_fmt.width = CAPTURE_W;
	cam_fmt.height = CAPTURE_H;
	cam_fmt.pixelformat = VIDEO_PIX_FMT_RGB565;
	ret = video_set_compose_format(camera_dev, &cam_fmt);
	if (ret < 0) {
		LOG_WRN("QVGA RGB565 not accepted (%d); using driver default", ret);
		ret = video_get_format(camera_dev, &cam_fmt);
		if (ret < 0) {
			return ret;
		}
		ret = video_set_compose_format(camera_dev, &cam_fmt);
		if (ret < 0) {
			LOG_ERR("cannot program default format either (%d)", ret);
			return ret;
		}
	}
	ret = video_get_format(camera_dev, &cam_fmt);
	if (ret < 0) {
		return ret;
	}

	if (cam_fmt.pixelformat != VIDEO_PIX_FMT_RGB565 &&
	    cam_fmt.pixelformat != VIDEO_PIX_FMT_YUYV &&
	    cam_fmt.pixelformat != VIDEO_PIX_FMT_BGRX32) {
		LOG_ERR("unsupported pixel format %s",
			VIDEO_FOURCC_TO_STR(cam_fmt.pixelformat));
		return -ENOTSUP;
	}
	if (cam_fmt.width < VISION_INPUT_W || cam_fmt.height < VISION_INPUT_H) {
		LOG_ERR("capture %ux%u smaller than model input",
			cam_fmt.width, cam_fmt.height);
		return -ENOTSUP;
	}

	LOG_INF("camera %s: %s %ux%u (pitch %u)", camera_dev->name,
		VIDEO_FOURCC_TO_STR(cam_fmt.pixelformat),
		cam_fmt.width, cam_fmt.height, cam_fmt.pitch);

	if (caps.min_vbuf_count > NUM_BUFS) {
		LOG_ERR("driver needs %u buffers, have %u",
			caps.min_vbuf_count, NUM_BUFS);
		return -EINVAL;
	}

	for (int i = 0; i < NUM_BUFS; i++) {
		struct video_buffer *vbuf;

		vbuf = video_buffer_aligned_alloc(cam_fmt.size,
						  CONFIG_VIDEO_BUFFER_POOL_ALIGN,
						  K_NO_WAIT);
		if (vbuf == NULL) {
			LOG_ERR("video buffer alloc failed (%u B); raise "
				"CONFIG_VIDEO_BUFFER_POOL_HEAP_SIZE", cam_fmt.size);
			return -ENOMEM;
		}
		vbuf->type = VIDEO_BUF_TYPE_OUTPUT;
		ret = video_enqueue(camera_dev, vbuf);
		if (ret < 0) {
			LOG_ERR("video_enqueue failed (%d)", ret);
			return ret;
		}
	}

	ret = video_stream_start(camera_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret < 0) {
		LOG_ERR("video_stream_start failed (%d)", ret);
		return ret;
	}

	k_thread_create(&drain_thread, drain_stack,
			K_THREAD_STACK_SIZEOF(drain_stack), drain_fn,
			NULL, NULL, NULL, K_PRIO_PREEMPT(10), 0, K_NO_WAIT);
	k_thread_name_set(&drain_thread, "cam_drain");

	return 0;
}

void vision_camera_stop(void)
{
	/* Quiesce the drain thread and the capture DMA (e.g. after a failed
	 * self-test) so a broken pipeline cannot keep writing. */
	cam_stopped = true;
	k_thread_join(&drain_thread, K_MSEC(2500));
	(void)video_stream_stop(camera_dev, VIDEO_BUF_TYPE_OUTPUT);
}

void vision_camera_get_format(uint32_t *pixelformat, uint16_t *width,
			      uint16_t *height)
{
	if (pixelformat != NULL) {
		*pixelformat = cam_fmt.pixelformat;
	}
	if (width != NULL) {
		*width = (uint16_t)cam_fmt.width;
	}
	if (height != NULL) {
		*height = (uint16_t)cam_fmt.height;
	}
}

/* --- frame reduction -------------------------------------------------------- */

/*
 * Area-average a centered square crop of the frame down to 96x96 luma.
 * The crop is height x height pixels (the largest square), so the model sees
 * an undistorted view. Integer box averaging: each destination pixel averages
 * the source box it maps onto.
 */
static void frame_to_gray96(const uint8_t *buf, uint8_t *gray96)
{
	const uint32_t crop = cam_fmt.height;               /* square side */
	const uint32_t x0 = (cam_fmt.width - crop) / 2;     /* crop origin  */

	for (uint32_t dy = 0; dy < VISION_INPUT_H; dy++) {
		uint32_t sy0 = dy * crop / VISION_INPUT_H;
		uint32_t sy1 = (dy + 1) * crop / VISION_INPUT_H;

		if (sy1 == sy0) {
			sy1 = sy0 + 1;
		}
		for (uint32_t dx = 0; dx < VISION_INPUT_W; dx++) {
			uint32_t sx0 = x0 + dx * crop / VISION_INPUT_W;
			uint32_t sx1 = x0 + (dx + 1) * crop / VISION_INPUT_W;
			uint32_t acc = 0, n = 0;

			if (sx1 == sx0) {
				sx1 = sx0 + 1;
			}
			for (uint32_t sy = sy0; sy < sy1; sy++) {
				const uint8_t *line = buf + sy * cam_fmt.pitch;

				for (uint32_t sx = sx0; sx < sx1; sx++) {
					acc += pix_luma(line, sx);
					n++;
				}
			}
			gray96[dy * VISION_INPUT_W + dx] = (uint8_t)(acc / n);
		}
	}
}

/* Box-average the full frame to grayscale for the snapshot path. The
 * decimation factor adapts to the capture size (2 for QVGA, 8 for 720p) so
 * the result always fits SNAPSHOT_MAX_W x SNAPSHOT_MAX_H; actual dimensions
 * ride in the snapshot metadata. */
static void frame_to_snapshot(const uint8_t *buf, uint8_t *snap,
			      uint16_t *snap_w, uint16_t *snap_h)
{
	uint32_t d = MAX(DIV_ROUND_UP(cam_fmt.width, SNAPSHOT_MAX_W),
			 DIV_ROUND_UP(cam_fmt.height, SNAPSHOT_MAX_H));
	uint16_t w = (uint16_t)(cam_fmt.width / d);
	uint16_t h = (uint16_t)(cam_fmt.height / d);

	for (uint32_t dy = 0; dy < h; dy++) {
		for (uint32_t dx = 0; dx < w; dx++) {
			uint32_t acc = 0;

			for (uint32_t sy = dy * d; sy < (dy + 1) * d; sy++) {
				const uint8_t *line = buf + sy * cam_fmt.pitch;

				for (uint32_t sx = dx * d; sx < (dx + 1) * d; sx++) {
					acc += pix_luma(line, sx);
				}
			}
			snap[dy * w + dx] = (uint8_t)(acc / (d * d));
		}
	}
	*snap_w = w;
	*snap_h = h;
}

int vision_camera_capture(uint8_t *gray96, uint8_t *snap, uint16_t *snap_w,
			  uint16_t *snap_h, int timeout_ms)
{
	int64_t deadline = k_uptime_get() + timeout_ms;

	/* Convert from the drain thread's held frame; wait for the first one. */
	do {
		k_mutex_lock(&latest_lock, K_FOREVER);
		if (latest != NULL) {
			frame_to_gray96(latest->buffer, gray96);
			if (snap != NULL) {
				frame_to_snapshot(latest->buffer, snap,
						  snap_w, snap_h);
			}
			k_mutex_unlock(&latest_lock);
			return 0;
		}
		k_mutex_unlock(&latest_lock);
		k_sleep(K_MSEC(50));
	} while (k_uptime_get() < deadline);

	return -EAGAIN;
}
