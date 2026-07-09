/*
 * Copyright (c) 2026 Avnet, Inc.
 * SPDX-License-Identifier: MIT
 *
 * eIQ predictive-maintenance -- vibration capture (Phase 1).
 *
 * The MIKROE ML Vibro Sens Click carries an NXP FXLS8974CF 3-axis accelerometer
 * (@0x18) AND two onboard DC vibration motors, so it is a self-contained PdM
 * training rig: this firmware DRIVES the motors through a labeled cycle and
 * captures the accelerometer, printing one CSV line per sample on the console:
 *
 *     VIB,<t_ms>,<state>,<ax_g>,<ay_g>,<az_g>
 *
 * The <state> column auto-labels the data for NXP eIQ Time Series Studio:
 *   idle       both motors off              (machine off / floor noise)
 *   balanced   balanced motor on            ("healthy" nominal vibration)
 *   unbalanced unbalanced motor pulsed      ("fault" -- imbalance signature)
 *   both       balanced + unbalanced        (loaded / compound fault)
 *
 * Capture this console log, then in eIQ Time Series Studio import it, use the
 * state column as the label, autoML a model, and export it. Phase 2 loads that
 * model, infers on the Neutron NPU, and streams the verdict to /IOTCONNECT.
 *
 * Network-free by design: capturing training data needs only I2C, GPIO + console.
 *
 * Motor pins (active-high GPIO, from the board overlay):
 *   BAL = balanced motor   -> mikroBUS CS  (gpio3 23)
 *   UNB = unbalanced motor -> mikroBUS PWM (gpio3 19)
 * Per MIKROE, the unbalanced motor should NOT be driven at continuous full
 * power, so UNB is software-pulsed (~30%% duty, 5 Hz) rather than held on.
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/printk.h>

#define SAMPLE_HZ   100
#define SAMPLE_MS   (1000 / SAMPLE_HZ)
#define G_PER_MS2   (1.0 / 9.80665)   /* Zephyr accel channels are m/s^2 */
#define STATE_MS    4000              /* hold each labeled state this long */

enum vib_state { ST_IDLE, ST_BALANCED, ST_UNBALANCED, ST_BOTH, ST_COUNT };
static const char *const state_name[ST_COUNT] = {
	"idle", "balanced", "unbalanced", "both",
};

static const struct device *const accel = DEVICE_DT_GET(DT_NODELABEL(fxls8974));
static const struct gpio_dt_spec bal =
	GPIO_DT_SPEC_GET(DT_NODELABEL(bal_motor), gpios);
static const struct gpio_dt_spec unb =
	GPIO_DT_SPEC_GET(DT_NODELABEL(unb_motor), gpios);

int main(void)
{
	printk("\n=== eIQ predictive-maintenance: vibration capture (FXLS8974) ===\n");
	printk("Driving the ML Vibro Sens motors through a labeled cycle and sampling\n");
	printk("at ~%d Hz. Capture this log, then autoML it in eIQ Time Series "
	       "Studio.\n\n", SAMPLE_HZ);

	if (!device_is_ready(accel)) {
		printk("ERROR: FXLS8974 accelerometer not ready\n");
		return 0;
	}
	if (!gpio_is_ready_dt(&bal) || !gpio_is_ready_dt(&unb)) {
		printk("ERROR: vibro-motor GPIOs not ready\n");
		return 0;
	}
	gpio_pin_configure_dt(&bal, GPIO_OUTPUT_INACTIVE);
	gpio_pin_configure_dt(&unb, GPIO_OUTPUT_INACTIVE);

	/* Raise the output data rate so each poll returns a fresh sample (the
	 * FXLS8974 driver takes the ODR on SENSOR_CHAN_ALL, not the XYZ channel). */
	struct sensor_value odr = { .val1 = 200, .val2 = 0 };

	if (sensor_attr_set(accel, SENSOR_CHAN_ALL,
			    SENSOR_ATTR_SAMPLING_FREQUENCY, &odr) != 0) {
		printk("WARN: could not set ODR (200 Hz); using sensor default\n");
	}

	printk("VIB,t_ms,state,ax_g,ay_g,az_g\n");

	int last_state = -1;

	while (1) {
		int64_t t = k_uptime_get();
		enum vib_state st = (t / STATE_MS) % ST_COUNT;

		if ((int)st != last_state) {
			last_state = st;
			printk("# state -> %s\n", state_name[st]);
		}

		/* Balanced motor: steady on for its states (safe continuous). */
		gpio_pin_set_dt(&bal, (st == ST_BALANCED || st == ST_BOTH) ? 1 : 0);
		/* Unbalanced motor: software-pulsed (~30%% duty @5 Hz) when active,
		 * so it is never held at continuous full power. */
		bool unb_active = (st == ST_UNBALANCED || st == ST_BOTH);

		gpio_pin_set_dt(&unb, (unb_active && (t % 200) < 60) ? 1 : 0);

		struct sensor_value a[3];

		if (sensor_sample_fetch(accel) == 0 &&
		    sensor_channel_get(accel, SENSOR_CHAN_ACCEL_XYZ, a) == 0) {
			printk("VIB,%lld,%s,%.4f,%.4f,%.4f\n", t, state_name[st],
			       sensor_value_to_double(&a[0]) * G_PER_MS2,
			       sensor_value_to_double(&a[1]) * G_PER_MS2,
			       sensor_value_to_double(&a[2]) * G_PER_MS2);
		}
		k_msleep(SAMPLE_MS);
	}
	return 0;
}
