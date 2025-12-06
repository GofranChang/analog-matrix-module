/*
 * Copyright 2025 Peter Johanson
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/kscan.h>
#include <zmk/drivers/kscan/zmk_analog_matrix.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define KSCAN_NODE DT_NODELABEL(kscan_he)

static const struct device *adc_dev = DEVICE_DT_GET(
		DT_NODELABEL(adc0));
static const struct device *kscan_dev = DEVICE_DT_GET(KSCAN_NODE);

static struct {
	uint32_t row;
	uint32_t col;
	bool pressed;
} last_cb_val;
static int callback_calls_count;

#define NUM_CHANNELS DT_PROP_LEN(KSCAN_NODE, io_channels)
#define NUM_SELECTS DT_PROP_LEN(KSCAN_NODE, select_gpios)

static K_SEM_DEFINE(val_sem, 1, 1);
static struct gpio_dt_spec selects[] = {DT_FOREACH_PROP_ELEM_SEP(KSCAN_NODE, select_gpios, GPIO_DT_SPEC_GET_BY_IDX, (,))};
static uint16_t adc_vals[NUM_SELECTS][NUM_CHANNELS] = {0};

static void kscan_callback(const struct device *dev, uint32_t row, uint32_t col,
			   bool pressed)
{
	callback_calls_count++;
	last_cb_val.row = row;
	last_cb_val.col = col;
	last_cb_val.pressed = pressed;
}

static int get_adc_val(const struct device *dev, unsigned int channel, void *user, uint32_t *val) {
	int ret = k_sem_take(&val_sem, K_MSEC(20));
	if (ret < 0) {
		return ret;
	}

	for (size_t s = 0; s < NUM_SELECTS; s++) {
		gpio_flags_t gpio_flags;
		gpio_pin_get_config_dt(&selects[s], &gpio_flags);
		if (gpio_flags & GPIO_INPUT) {
			*val = adc_vals[channel][s];
			ret = 0;
			goto get_adc_exit;
		}
	}

	ret = -EINVAL;

get_adc_exit:
	k_sem_give(&val_sem);

	return ret;
}

static int set_val(uint8_t channel, uint8_t select, uint16_t val) {
	__ASSERT(channel < NUM_CHANNELS, "Invalid channel");
	__ASSERT(select < NUM_SELECTS, "Invalid select");

	int ret = k_sem_take(&val_sem, K_MSEC(20));
	if (ret < 0) {
		return ret;
	}

	adc_vals[channel][select] = val;

	k_sem_give(&val_sem);

	return 0;
}

ZTEST(kscan_he_calibrator, test_key_press_without_calibration)
{
	adc_emul_raw_value_func_set(adc_dev, 0, get_adc_val, NULL);
	adc_emul_raw_value_func_set(adc_dev, 1, get_adc_val, NULL);

	kscan_config(kscan_dev, kscan_callback);
	kscan_enable_callback(kscan_dev);

	k_sleep(K_MSEC(100));
	zassert_equal(callback_calls_count, 0);

	set_val(0, 0, 2700);
	k_sleep(K_MSEC(100));

	zassert_equal(callback_calls_count, 0);

	set_val(0, 1, 2700);
	k_sleep(K_MSEC(100));

	zassert_equal(callback_calls_count, 0);

	set_val(0, 0, 20);
	k_sleep(K_MSEC(100));

	zassert_equal(callback_calls_count, 0);
}

static struct zmk_analog_matrix_calibration_event last_calib_ev;

static void calibration_cb(const struct zmk_analog_matrix_calibration_event *ev, const void *user_data) {
	memcpy(&last_calib_ev, ev, sizeof(struct zmk_analog_matrix_calibration_event));
}

ZTEST(kscan_he_calibrator, test_key_press_after_calibration)
{
	adc_emul_raw_value_func_set(adc_dev, 0, get_adc_val, NULL);
	adc_emul_raw_value_func_set(adc_dev, 1, get_adc_val, NULL);

	kscan_config(kscan_dev, kscan_callback);
	kscan_enable_callback(kscan_dev);

	set_val(0, 0, 20);
	set_val(0, 1, 25);
	set_val(1, 0, 28);
	set_val(1, 1, 21);

	zmk_analog_matrix_calibrate(kscan_dev, calibration_cb, NULL);

	k_sleep(K_SECONDS(5));

	zassert_equal(last_calib_ev.type, CALIBRATION_EV_HIGH_SAMPLING_START);

	// TODO: Assert we get the callbacks!
	set_val(0, 0, 2950);
	k_sleep(K_MSEC(1));
	set_val(0, 0, 2951);
	k_sleep(K_SECONDS(1));

	zassert_equal(last_calib_ev.type, CALIBRATION_EV_POSITION_COMPLETE);
	zassert_equal(last_calib_ev.data.position_complete.select, 0);
	zassert_equal(last_calib_ev.data.position_complete.input, 0);

	set_val(0, 0, 20);
	k_sleep(K_MSEC(100));

	set_val(0, 1, 2950);
	k_sleep(K_SECONDS(1));
	set_val(0, 1, 20);
	k_sleep(K_MSEC(100));

	zassert_equal(last_calib_ev.type, CALIBRATION_EV_POSITION_COMPLETE);
	zassert_equal(last_calib_ev.data.position_complete.select, 1);
	zassert_equal(last_calib_ev.data.position_complete.input, 0);

	set_val(1, 0, 2950);
	k_sleep(K_SECONDS(1));
	set_val(1, 0, 20);
	k_sleep(K_MSEC(100));

	zassert_equal(last_calib_ev.type, CALIBRATION_EV_POSITION_COMPLETE);
	zassert_equal(last_calib_ev.data.position_complete.select, 0);
	zassert_equal(last_calib_ev.data.position_complete.input, 1);


	set_val(1, 1, 2950);
	k_sleep(K_SECONDS(1));
	set_val(1, 1, 20);

	k_sleep(K_SECONDS(1));

	zassert_equal(last_calib_ev.type, CALIBRATION_EV_COMPLETE);

	/* Clear any events from the last calibration key held down */
	memset(&last_cb_val, 0, sizeof(last_cb_val));
	callback_calls_count = 0;

	// TODO: Assert calibration complete

	set_val(0, 0, 2700);
	k_sleep(K_MSEC(100));

	zassert_equal(callback_calls_count, 1);
	zassert_equal(last_cb_val.row, 0);
	zassert_equal(last_cb_val.col, 0);
	zassert_equal(last_cb_val.pressed, true);

	set_val(0, 1, 2700);

	k_sleep(K_MSEC(100));

	zassert_equal(callback_calls_count, 2);
	zassert_equal(last_cb_val.row, 0);
	zassert_equal(last_cb_val.col, 1);
	zassert_equal(last_cb_val.pressed, true);

	set_val(0, 0, 20);
	k_sleep(K_MSEC(100));

	zassert_equal(callback_calls_count, 3);
	zassert_equal(last_cb_val.row, 0);
	zassert_equal(last_cb_val.col, 0);

	set_val(0, 1, 21);

	k_sleep(K_MSEC(100));

	zassert_equal(callback_calls_count, 4);
	zassert_equal(last_cb_val.row, 0);
	zassert_equal(last_cb_val.col, 1);
}

void clear_cb(const struct device *dev, struct zmk_analog_matrix_calibration_entry *entries, size_t len, const void *user_data) {
	memset(entries, 0, len * sizeof(struct zmk_analog_matrix_calibration_entry));
}

static void clear_events(void *data) {
	zmk_analog_matrix_access_calibration(kscan_dev, clear_cb, NULL);
	memset(&last_cb_val, 0, sizeof(last_cb_val));
	memset(&last_calib_ev, 0, sizeof(last_calib_ev));
	callback_calls_count = 0;
}

static void disable_kscan(void *data) {
	kscan_disable_callback(kscan_dev);
}

ZTEST_SUITE(kscan_he_calibrator, NULL, NULL, clear_events, disable_kscan, NULL);
