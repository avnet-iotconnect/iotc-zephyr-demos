/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Camera pipeline for the vision-occupancy demo.
 *
 * Wraps the Zephyr video API (chosen zephyr,camera -- on the RT1170-EVKB the
 * OV5640 shield module behind MIPI CSI-2) and reduces each captured frame to
 * the two things the app needs:
 *   - a 96x96 8-bit grayscale model input (area-averaged downscale of a
 *     center square crop), and
 *   - on request, a full-frame grayscale snapshot (2x2-decimated) for the
 *     cloud snapshot transfer.
 *
 * The capture format is requested as QVGA (320x240) RGB565; if the driver
 * negotiates something else the conversion adapts (RGB565 or YUYV, any
 * resolution >= 96x96).
 */
#ifndef VISION_CAMERA_H
#define VISION_CAMERA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VISION_INPUT_W 96
#define VISION_INPUT_H 96
#define VISION_INPUT_LEN (VISION_INPUT_W * VISION_INPUT_H)

/* Snapshot output: capture frame decimated 2x2 (QVGA -> 160x120). */
#define SNAPSHOT_MAX_W 320
#define SNAPSHOT_MAX_H 180
#define SNAPSHOT_MAX_LEN (SNAPSHOT_MAX_W * SNAPSHOT_MAX_H)

/*
 * Initialize the camera: negotiate the format, allocate + enqueue capture
 * buffers, start streaming. Returns 0 on success or a negative errno.
 */
int vision_camera_init(void);

/* Stop streaming (quiesce capture DMA), e.g. after a failed self-test. */
void vision_camera_stop(void);

/* Negotiated capture format (valid after vision_camera_init). */
void vision_camera_get_format(uint32_t *pixelformat, uint16_t *width,
			      uint16_t *height);

/*
 * Capture one frame and downscale it into gray96 (VISION_INPUT_LEN bytes).
 *
 * If snap is non-NULL, the same frame is also decimated 2x2 to grayscale
 * into snap (up to SNAPSHOT_MAX_LEN bytes) and *snap_w / *snap_h are set.
 *
 * Blocks up to timeout_ms for the next frame. Returns 0 on success,
 * -EAGAIN on timeout, or a negative errno.
 */
int vision_camera_capture(uint8_t *gray96, uint8_t *snap, uint16_t *snap_w,
			  uint16_t *snap_h, int timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* VISION_CAMERA_H */
