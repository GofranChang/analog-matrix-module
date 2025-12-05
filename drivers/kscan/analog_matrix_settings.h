/*
 * Copyright (c) 2025 Peter Johanson
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>

int zmk_analog_matrix_settings_load_calibration(const struct device *dev);
int zmk_analog_matrix_settings_save_calibration(const struct device *dev);
