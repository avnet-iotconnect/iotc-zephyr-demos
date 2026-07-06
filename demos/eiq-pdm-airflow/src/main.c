/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * eIQ predictive-maintenance -- airflow dP capture (Phase 1).
 *
 * Samples the MIKROE Ultra-Low Press Click (Sensirion SDP810, @0x6C) on the
 * FRDM-MCXN947 mikroBUS and prints one CSV line per sample on the console:
 *
 *     PDM,<t_ms>,<dp_pa>,<temp_c>
 *
 * Run this while you enact "normal", "clogging", "leak", "fan-fault" episodes on
 * the airflow rig (pump/fan + tube + adjustable restriction), capture the
 * console log, then import + label it in NXP eIQ Time Series Studio to train an
 * anomaly / predictive-maintenance model. Phase 2 loads that model and streams
 * the anomaly verdict to /IOTCONNECT (see README.md).
 *
 * Network-free by design: capturing training data needs only I2C + the console.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#define SDP810_ADDR   0x6Cu
#define SAMPLE_HZ     10
#define SAMPLE_MS     (1000 / SAMPLE_HZ)

static const struct device *const bus = DEVICE_DT_GET(DT_NODELABEL(mikrobus_i2c));

/* SDP810 (Ultra-Low Press Click): clear STATUS (0x36), wait until a measurement
 * is ready, then read temperature (0x2E) and differential pressure (0x30) as
 * 2-byte words. Conversion matches the Click's reference reader. */
static bool sdp810_read(double *dp_pa, double *temp_c)
{
	uint8_t clr[3] = {0x36, 0xFF, 0xFF};
	uint8_t reg, w[2] = {0};

	if (i2c_write(bus, clr, sizeof(clr), SDP810_ADDR) != 0) {
		return false;
	}
	k_msleep(10);
	for (int i = 0; i < 20; i++) {
		uint8_t sreg = 0x36, st[2] = {0};

		if (i2c_write_read(bus, SDP810_ADDR, &sreg, 1, st, 2) == 0) {
			uint16_t status = (uint16_t)((st[0] << 8) | st[1]);

			if ((status & 0x0010U) && (status & 0x0008U)) {
				break;
			}
		}
		k_msleep(5);
	}

	reg = 0x2E;
	if (i2c_write_read(bus, SDP810_ADDR, &reg, 1, w, 2) != 0) {
		return false;
	}
	int16_t traw = (int16_t)((w[0] << 8) | w[1]);

	reg = 0x30;
	if (i2c_write_read(bus, SDP810_ADDR, &reg, 1, w, 2) != 0) {
		return false;
	}
	int16_t praw = (int16_t)((w[0] << 8) | w[1]);

	*dp_pa = -20.0 + (((double)praw + 26215.0) / 52429.0) * 520.0;
	*temp_c = ((double)traw + 16881.0) / 397.2;
	return true;
}

int main(void)
{
	printk("\n=== eIQ predictive-maintenance: airflow dP capture (SDP810) ===\n");
	printk("Sampling the Ultra-Low Press Click at ~%d Hz. Capture this console\n",
	       SAMPLE_HZ);
	printk("log while running rig episodes, then label it in eIQ Time Series "
	       "Studio.\n\n");

	if (!device_is_ready(bus)) {
		printk("ERROR: mikroBUS I2C bus not ready\n");
		return 0;
	}

	/* CSV header (drop it on import if TSS expects data rows only). */
	printk("PDM,t_ms,dp_pa,temp_c\n");

	while (1) {
		int64_t t = k_uptime_get();
		double dp, temp;

		if (sdp810_read(&dp, &temp)) {
			printk("PDM,%lld,%.3f,%.2f\n", t, dp, temp);
		} else {
			/* Empty fields on a read miss keep the CSV columns aligned. */
			printk("PDM,%lld,,\n", t);
		}
		k_msleep(SAMPLE_MS);
	}
	return 0;
}
