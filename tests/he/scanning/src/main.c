/*
 * Copyright 2025 Peter Johanson
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/kscan.h>
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

ZTEST(kscan_he_scanning, test_key_press_sequence)
{
	adc_emul_raw_value_func_set(adc_dev, 0, get_adc_val, NULL);
	adc_emul_raw_value_func_set(adc_dev, 1, get_adc_val, NULL);

	kscan_config(kscan_dev, kscan_callback);
	kscan_enable_callback(kscan_dev);

	k_sleep(K_MSEC(100));
	zassert_equal(callback_calls_count, 0);

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
	zassert_equal(last_cb_val.pressed, false);

}

ZTEST_SUITE(kscan_he_scanning, NULL, NULL, NULL, NULL, NULL);
