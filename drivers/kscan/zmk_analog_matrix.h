/*
 * Copyright (c) 2025 Peter Johanson
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/drivers/kscan.h>
#include <zmk/drivers/kscan/zmk_analog_matrix.h>

typedef uint16_t (*zmk_analog_matrix_read_raw_cb_t)(const struct device *dev, uint8_t select,
                                                    uint8_t strobe);
typedef void (*zmk_analog_matrix_power_cb_t)(const struct device *dev, bool on);
typedef uint8_t (*zmk_analog_matrix_input_resolution_cb_t)(const struct device *dev, uint8_t input);

typedef void (*zmk_analog_matrix_value_cb_t)(const struct device *dev, uint8_t select,
                                             uint8_t input, uint16_t val, void *user_data);

typedef void (*zmk_analog_matrix_scan_cb_t)(const struct device *dev,
                                            zmk_analog_matrix_value_cb_t cb, void *user_data);

struct zmk_analog_matrix_common_cfg {
    const uint8_t inputs_len;
    const uint8_t selects_len;
    /* Must be an array that is `inputs_len` long, and is a bitmask of select indices to ignore/skip
     */
    const uint32_t *input_select_masks;

    const uint8_t trigger_percentage;
    const bool high_threshold_noise_based;
    const int high_threshold_noise_mult;

    const uint16_t active_polling_interval_ms;

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)
    const uint16_t idle_polling_interval_ms;
    const uint16_t sleep_polling_interval_ms;
    const uint16_t idle_after_secs;
    const uint16_t sleep_after_secs;
    const bool dynamic_polling_interval;
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)

    zmk_analog_matrix_read_raw_cb_t read;
    zmk_analog_matrix_scan_cb_t scan;
    zmk_analog_matrix_power_cb_t power;
    zmk_analog_matrix_input_resolution_cb_t input_resolution;
};

struct zmk_analog_matrix_common_data {
    kscan_callback_t callback;
    struct k_thread thread;
    struct k_mutex mutex;
    uint16_t poll_interval;
    struct zmk_analog_matrix_calibration_entry *calibrations;
    uint64_t *reported_matrix_state;
#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_CALIBRATOR)
    zmk_analog_matrix_calibration_cb_t calibration_callback;
    const void *calibration_user_data;
    zmk_analog_matrix_sample_cb_t sample_callback;
    uint8_t sample_strobe;
    uint8_t sample_select;
    uint16_t sample_times;
    const void *sample_user_data;
#endif // IS_DEFINED(CONFIG_ZMK_ANALOG_MATRIX_CALIBRATOR)

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)
    uint32_t last_key_released_at;
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SCAN_RATE_CALC)
    uint64_t max_scan_duration_ns;
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SCAN_RATE_CALC)

    uint64_t *matrix_state;
};

int zmk_analog_matrix_configure(const struct device *dev, kscan_callback_t callback);
int zmk_analog_matrix_enable(const struct device *dev);
int zmk_analog_matrix_disable(const struct device *dev);

struct zmk_analog_matrix_calibration_entry *
zmk_analog_matrix_calibration_entry_for_sel_str(const struct device *dev, uint8_t select,
                                                uint8_t strobe);
bool zmk_analog_matrix_valid_sel_str(const struct device *dev, uint8_t sel, uint8_t str,
                                     bool need_calibration);
void zmk_analog_matrix_thread_main(void *arg1, void *unused1, void *unused2);

int zmk_analog_matrix_init(const struct device *dev);

uint64_t zmk_analog_matrix_max_scan_duration_ns(const struct device *dev);
