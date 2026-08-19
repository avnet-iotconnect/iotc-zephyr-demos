/**
 * Copyright 2019-2025 EBV Elektronik. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. The name of EBV Elektronik may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY EBV "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT ARE
 * EXPRESSLY AND SPECIFICALLY DISCLAIMED. IN NO EVENT SHALL EBV BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/** System includes. */
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(facedet, LOG_LEVEL_INF);

#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/device.h>
#include <zephyr/random/random.h>
#include <zephyr/dfu/mcuboot.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/drivers/video.h>
#include <string.h>
#include <strings.h>



#include "iotc_thread.h"
#define IOTC_INF_STACKSIZE 4096
K_THREAD_STACK_DEFINE(iotc_thread_stack_area, IOTC_INF_STACKSIZE);
static struct k_thread iotc_thread_data;
struct k_fifo iotc_fifo;


#define FACEDETECT_DEMO
#ifdef FACEDETECT_DEMO

#include "FacialDetect.h"
struct display_buffer_descriptor strbuf_desc;

#include <zephyr/drivers/display.h>
#include <zephyr/drivers/video.h>
#include "display_app.h"
#include "convert.h"

const struct device *display_dev;

/* Upstream nxp,video-smartdma delivers QVGA frames as 320x30 RGB565 slices
 * (kSMARTDMA_CameraDiv16FrameQVGA: two 15-line firmware stripes per buffer,
 * 8 buffers per frame), reporting each slice's position via
 * video_buffer.line_offset.
 */
#define SLICE_LINES 30

volatile static uint32_t __attribute((aligned(4))) vbuf_idx = 0;

#include "model.h"

K_THREAD_STACK_DEFINE(inf_thread_stack_area, THREAD_INF_STACKSIZE);
static struct k_thread inf_thread_data;

struct k_fifo inf_fifo;  /* FIFO for buffers to inference */
#endif


/**
 * \brief Application main task function.
 */
int main(void)
{
    /* Application welcome message. */
    printk("EBV IoTConnect NXP " CONFIG_BOARD_TARGET "\n");

    /* Creating and starting iotc thread */
    k_fifo_init(&iotc_fifo);

	k_thread_create(&iotc_thread_data, iotc_thread_stack_area,
		K_THREAD_STACK_SIZEOF(iotc_thread_stack_area),
		iotc_thread, NULL, &iotc_fifo, NULL,
		THREAD_PRIORITY_INF, 1, K_FOREVER);
	k_thread_name_set(&iotc_thread_data, "iotc_thread");

    k_thread_start(&iotc_thread_data);

    struct video_buffer *buffers[CONFIG_VIDEO_BUFFER_POOL_NUM_MAX], *vbuf;
	struct display_buffer_descriptor buf_desc;
	void *convdisp_buf = NULL;
	rect_buf_params_t rect_params;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return -1;
	}

	if (convdisp_init(DT_PROP(DT_CHOSEN(zephyr_display), width),
		DT_PROP(DT_CHOSEN(zephyr_display), height))) {
		LOG_ERR("convdisp_init() failed\n");
		return -1;
	}

	const struct device *const video = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));

	if (!device_is_ready(video)) {
		LOG_ERR("%s: device not ready.\n", video->name);
		return -1;
	}

#if DT_NODE_EXISTS(DT_NODELABEL(ov7670))
	/* The sensor's init is deferred (see the board overlay): its XCLK is
	 * only running once the video device has applied the camera pinmux.
	 * Give the sensor a few tries -- it needs some XCLK cycles after
	 * power-down/reset deassert before its SCCB interface responds.
	 */
	const struct device *const sensor = DEVICE_DT_GET(DT_NODELABEL(ov7670));

	for (int try = 0; !device_is_ready(sensor); try++) {
		if (try >= 5) {
			LOG_ERR("%s: sensor init failed.\n", sensor->name);
			return -1;
		}
		k_msleep(50);
		(void)device_init(sensor);
	}
#endif

	int i = 0;
	unsigned int frame = 0;

	LOG_INF("- Device name: %s\n", video->name);

	/* Get capabilities */
	struct video_caps caps = {.type = VIDEO_BUF_TYPE_OUTPUT};
	if (video_get_caps(video, &caps)) {
		LOG_ERR("Unable to retrieve video capabilities");
		return -1;
	}

	/* Get default/native format */
	struct video_format fmt = {.type = VIDEO_BUF_TYPE_OUTPUT};
	if (video_get_format(video, &fmt)) {
		LOG_ERR("Unable to retrieve video format");
		return -1;
	}

	/* Program the pipeline (including the sensor) with that format --
	 * without this the OV7670 free-runs in its power-on format and the
	 * capture engine slices a mismatched bitstream into static.
	 */
	if (video_set_format(video, &fmt)) {
		LOG_ERR("Unable to set video format");
		return -1;
	}

	/* The SmartDMA capture engine streams fixed-height slices */
	const uint8_t slices_per_frame = fmt.height / SLICE_LINES;

	LOG_INF("- Default format: %c%c%c%c %ux%u\n", (char)fmt.pixelformat,
	       (char)(fmt.pixelformat >> 8),
	       (char)(fmt.pixelformat >> 16),
	       (char)(fmt.pixelformat >> 24),
	       fmt.width, fmt.height);

	/* Size to allocate for each buffer. The SmartDMA camera engine writes
	 * 36 bytes of metadata past the frame data (the original demo sized
	 * buffers "320W x 15H x 2BPP x 2(SmartDMA) + 36") -- without the slack
	 * every buffer overruns into adjacent RAM and corrupts whatever the
	 * linker placed there.
	 */
	size_t bsize;
	bsize = fmt.pitch * SLICE_LINES + 36U;

	/* Alloc video buffers and enqueue for capture */
	for (i = 0; i < ARRAY_SIZE(buffers); i++) {
		buffers[i] = video_buffer_alloc(bsize, K_FOREVER);
		if (buffers[i] == NULL) {
			LOG_ERR("Unable to alloc video buffer");
			return -1;
		}

		buffers[i]->type = VIDEO_BUF_TYPE_OUTPUT;
		video_enqueue(video, buffers[i]);
	}

	/* configure display buffer */
	uint16_t y_step;

	y_step = SLICE_LINES;
	buf_desc.buf_size = bsize;
	buf_desc.width = fmt.width;
	buf_desc.pitch = fmt.width;
	buf_desc.height = y_step;
	#ifdef CONFIG_APP_DISPLAY_BANNER
		buf_desc.height -= BANNER_HEIGHT;
	#endif

	/* use a video buffer to initially draw the display brackground */
	display_background(buffers[0]->buffer);

	/* Start video capture */
	if (video_stream_start(video, VIDEO_BUF_TYPE_OUTPUT)) {
		LOG_ERR("Unable to start capture (interface)");
		return -1;
	}
	LOG_INF("Capture started\n");

	/* Create inference thread */
	inf_thread_config_t inf_config;

	inf_config.frameWidth = fmt.width;
	inf_config.fullFrameHeight = fmt.height;
	inf_config.singleFrameHeight = buf_desc.height;

	k_fifo_init(&inf_fifo);

	k_thread_create(&inf_thread_data, inf_thread_stack_area,
		K_THREAD_STACK_SIZEOF(inf_thread_stack_area),
		inference_thread, &inf_config, &inf_fifo, NULL,
		THREAD_PRIORITY_INF, 0, K_FOREVER);
	k_thread_name_set(&inf_thread_data, "inference_thread");

	uint8_t inf_frame_count = 0;
	bool inference_enabled = false;

	inf_results_t infResults, res_copy;
	inf_results_t *frame_infResults;
	if (slices_per_frame > 1U) {
		/* Inference results are synced at the start of each full video
		 * frame, to keep rectangles consistent in that frame.  Display
		 * will use the local copy of the results. */
		frame_infResults = &infResults;
	} else {
		/* When video provides full frame, display can use the global
		 * inference results, and avoid copying the results every frame */
		frame_infResults = &g_InfResults;
	}

	rect_params.width = buf_desc.width;
	rect_params.height = buf_desc.height;
	rect_params.slices_per_frame = slices_per_frame;

	/* video_dequeue selects the queue from the passed buffer's type */
	vbuf = buffers[0];

    /* Program main loop. */
    while (1) {

        int err;
        uint8_t slice_idx;

        /* Grab video frames */
        err = video_dequeue(video, &vbuf, K_FOREVER);
        if (err) {
            LOG_ERR("Unable to dequeue video buf");
            return -1;
        }
        slice_idx = vbuf->line_offset / y_step;

        LOG_DBG("\rGot frame %u! size: %u; timestamp %u ms",
            frame++, vbuf->bytesused, vbuf->timestamp);

        convdisp(vbuf->buffer, (uint8_t **) &convdisp_buf,
            vbuf->bytesused, K_FOREVER);

        if(slices_per_frame > 1) {
            if(slice_idx == 0) {
                sync_slice_rectangles(frame_infResults);
            }
        #if DT_HAS_CHOSEN(zephyr_modelbuf)
            copy_slice_to_model_input(slice_idx,
                (uint32_t) convdisp_buf, fmt.width, y_step,
                slices_per_frame);
        #endif /* #if DT_HAS_CHOSEN(zephyr_modelbuf) */
        }

        rect_params.buf = (uint16_t *) convdisp_buf;
        rect_params.frame_idx = slice_idx;
        display_rectangles(&rect_params, frame_infResults);

        /* Write the video buffer out to the display */
        display_write(display_dev, 0, y_step * slice_idx, &buf_desc, convdisp_buf);

        if(inference_enabled) {
            /* wake inf_thread if it is waiting for buffer */
            k_fifo_alloc_put(&inf_fifo, convdisp_buf);

            // send a copy of the inference results to iotc thread when at least 1 face was detected
            if (frame_infResults->odRetCnt > 0) {
                memcpy(&res_copy, frame_infResults, sizeof(res_copy));
                k_fifo_alloc_put(&iotc_fifo, &res_copy);
            }
        } else {
            if(inf_frame_count >= slices_per_frame) {
                /* wait for first full frame before starting
                * inference thread */
                k_fifo_alloc_put(&inf_fifo, convdisp_buf);
                k_thread_start(&inf_thread_data);
                inference_enabled = true;
            } else {
                inf_frame_count++;
                /* free buffer for next conversion */
                convdisp_enqueue(convdisp_buf);
            }
        }

        /* vbuf already used, re-use this buffer to write results to
        * display, before giving back to video driver. */
        display_results((uint16_t *)vbuf->buffer);
        err = video_enqueue(video, vbuf);
        if (err) {
            LOG_ERR("Unable to requeue video buf");
            break;
        }

    }
    LOG_INF("Main return\n");
    return 0;
}
