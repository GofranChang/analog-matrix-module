/*
 * Copyright (c) 2025 Peter Johanson
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/drivers/kscan.h>

struct zmk_analog_matrix_calibration_entry {
    uint16_t avg_low;
    uint16_t avg_high;
    uint16_t noise;
};

struct zmk_analog_matrix_calibration_event {
    enum zmk_analog_matrix_calibration_event_type {
        CALIBRATION_EV_LOW_SAMPLING_START,
        CALIBRATION_EV_HIGH_SAMPLING_START,
        CALIBRATION_EV_POSITION_LOW_DETERMINED,
        CALIBRATION_EV_POSITION_COMPLETE,
        CALIBRATION_EV_COMPLETE,
    } type;

    union zmk_analog_matrix_calibration_event_data {
        struct {
            uint8_t select;
            uint8_t input;

            int16_t low_avg;
            int16_t noise;
            int16_t snr;
        } position_low_determined;
        struct {
            uint8_t select;
            uint8_t input;

            int16_t low_avg;
            int16_t high_avg;
            int16_t noise;
            int16_t snr;
        } position_complete;

        struct {

        } calibration_complete;
    } data;
};

typedef void (*zmk_analog_matrix_sample_cb_t)(uint16_t val, const void *);
typedef void (*zmk_analog_matrix_calibration_access_cb_t)(const struct device *dev, struct zmk_analog_matrix_calibration_entry *entries, size_t len, const void *user_data);

typedef void (*zmk_analog_matrix_calibration_cb_t)(const struct zmk_analog_matrix_calibration_event *ev, const void *);
int zmk_analog_matrix_access_calibration(const struct device *dev, zmk_analog_matrix_calibration_access_cb_t cb, const void *user_data);

int zmk_analog_matrix_calibrate(const struct device *dev,
                                  zmk_analog_matrix_calibration_cb_t callback,
                                  const void *user_data);
int zmk_analog_matrix_sample(const struct device *dev,
		uint8_t select,
		uint8_t input,
		uint16_t times,
                                  zmk_analog_matrix_sample_cb_t callback,
                                  const void *user_data);
