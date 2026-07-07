/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * eIQ predictive-maintenance -- vibration capture (Phase 1).
 *
 * Samples the MIKROE ML Vibro Sens Click (NXP FXLS8974CF 3-axis accelerometer,
 * @0x18) on the FRDM-MCXN947 mikroBUS and prints one CSV line per sample:
 *
 *     VIB,<t_ms>,<ax_g>,<ay_g>,<az_g>
 *
 * This is NXP's reference vibration-anomaly setup (eIQ Time Series Studio /
 * Smart Fan). Mount the board/sensor on the machine (e.g. a fan or motor), run
 * this while staging "healthy" and "fault" states -- imbalance (add a scrap of
 * tape to a blade), blocked flow, bearing wear, off -- capture the console log,
 * and label it in eIQ Time Series Studio to train an anomaly / predictive-
 * maintenance model. Phase 2 loads that model, infers on the Neutron NPU, and
 * streams the verdict to /IOTCONNECT.
 *
 * Network-free by design: capturing training data needs only I2C + the console.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>

#define SAMPLE_HZ   100
#define SAMPLE_MS   (1000 / SAMPLE_HZ)
#define G_PER_MS2   (1.0 / 9.80665)   /* Zephyr accel channels are m/s^2 */

static const struct device *const accel = DEVICE_DT_GET(DT_NODELABEL(fxls8974));

int main(void)
{
	printk("\n=== eIQ predictive-maintenance: vibration capture (FXLS8974) ===\n");
	printk("Sampling the ML Vibro Sens Click at ~%d Hz. Capture this console log\n",
	       SAMPLE_HZ);
	printk("while staging machine states, then label it in eIQ Time Series "
	       "Studio.\n\n");

	if (!device_is_ready(accel)) {
		printk("ERROR: FXLS8974 accelerometer not ready\n");
		return 0;
	}

	/* Ask for a higher output data rate if the driver supports it (best-effort;
	 * ignored if unsupported -- capture still works at the default ODR). */
	struct sensor_value odr = { .val1 = 400, .val2 = 0 };

	(void)sensor_attr_set(accel, SENSOR_CHAN_ACCEL_XYZ,
			      SENSOR_ATTR_SAMPLING_FREQUENCY, &odr);

	printk("VIB,t_ms,ax_g,ay_g,az_g\n");

	while (1) {
		int64_t t = k_uptime_get();
		struct sensor_value a[3];

		if (sensor_sample_fetch(accel) == 0 &&
		    sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, a) == 0) {
			printk("VIB,%lld,%.4f,%.4f,%.4f\n", t,
			       sensor_value_to_double(&a[0]) * G_PER_MS2,
			       sensor_value_to_double(&a[1]) * G_PER_MS2,
			       sensor_value_to_double(&a[2]) * G_PER_MS2);
		} else {
			printk("VIB,%lld,,,\n", t);
		}
		k_msleep(SAMPLE_MS);
	}
	return 0;
}
