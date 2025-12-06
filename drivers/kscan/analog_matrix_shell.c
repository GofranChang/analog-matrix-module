/*
 * Copyright (c) 2025 Peter Johanson
 *
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include "zmk_analog_matrix.h"
#include "analog_matrix_settings.h"
#include "analog_matrix_shell.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(analog_matrix_shell);

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_CALIBRATOR)

static void calibrate_cb(const struct zmk_analog_matrix_calibration_event *ev,
                         const void *user_data) {
    const struct shell *sh = (const struct shell *)user_data;

    switch (ev->type) {
    case CALIBRATION_EV_LOW_SAMPLING_START:
        shell_prompt_change(sh, "-");
        shell_print(sh, "Low value sampling begins. Please do not press any keys");
        k_sleep(K_SECONDS(1));
        break;
    case CALIBRATION_EV_HIGH_SAMPLING_START:
        shell_prompt_change(sh, "-");
        shell_print(sh, "\nHigh value sampling begins. Please slowly press each key in sequence, "
                        "releasing once an asterisk appears");
        break;
    case CALIBRATION_EV_POSITION_LOW_DETERMINED:
#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_VERBOSE_CALIBRATOR)
        shell_print(sh, "Key at (%d,%d) is calibrated with avg low %d, noise: %d",
                    ev->data.position_low_determined.select, ev->data.position_low_determined.input,
                    ev->data.position_low_determined.low_avg,
                    ev->data.position_low_determined.noise);
#else
        shell_fprintf(sh, SHELL_NORMAL, "*");
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_VERBOSE_CALIBRATOR)
        break;
    case CALIBRATION_EV_POSITION_COMPLETE:
#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_VERBOSE_CALIBRATOR)
        shell_print(sh,
                    "Key at (%d,%d) is calibrated with avg low %d, avg high %d, noise: %d, SNR: %d",
                    ev->data.position_complete.select, ev->data.position_complete.input,
                    ev->data.position_complete.low_avg, ev->data.position_complete.high_avg,
                    ev->data.position_complete.noise, ev->data.position_complete.snr);
#else
        shell_fprintf(sh, SHELL_NORMAL, "*");
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_VERBOSE_CALIBRATOR)
        break;
    case CALIBRATION_EV_COMPLETE:
        shell_prompt_change(sh, CONFIG_SHELL_PROMPT_UART);
        shell_print(sh, "\nCalibration complete!");
        break;
    }
}

int zmk_analog_matrix_cmd_calibration_start(const struct shell *shell, size_t argc, char **argv,
                                        void *data) {
    const struct device *dev = device_get_binding(argv[-2]);
    if (!dev) {
        shell_error(shell, "Failed to find device named %s", argv[-2]);
	return -ENODEV;
    }

    int ret = zmk_analog_matrix_calibrate(dev, &calibrate_cb, shell);
    if (ret < 0) {
        shell_print(shell, "Failed to start calibration (%d)", ret);
    }

    return ret;
}

static void sample_cb(uint16_t val,
                         const void *user_data) {
    const struct shell *sh = (const struct shell *)user_data;

    shell_print(sh, "Val: %d", val);
}

int zmk_analog_matrix_cmd_sample(const struct shell *shell, size_t argc, char **argv,
                                         void *data) {
    uint8_t select;
    uint8_t input;
    uint16_t times;
    const struct device *dev = device_get_binding(argv[-1]);

    if (!dev) {
        shell_error(shell, "Failed to find device named %s", argv[-1]);
	return -ENODEV;
    }

    input = strtol(argv[1], NULL, 10);
    select = strtol(argv[2], NULL, 10);

    times = (argc == 4) ? strtol(argv[3], NULL, 10) : 10;

    shell_print(shell, "Got a sample for %d,%d with %d times", select, input, times);

    int ret = zmk_analog_matrix_sample(dev, select, input, times, &sample_cb, shell);
    if (ret < 0) {
        shell_print(shell, "Failed to start sampling (%d)", ret);
    }

    return ret;
}
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_CALIBRATOR)

static void export_cb(const struct device *dev,
                      struct zmk_analog_matrix_calibration_entry *entries, size_t len,
                      const void *user_data) {
    const struct shell *shell = (const struct shell *)user_data;

    shell_print(shell, "\tprecalib-avg-highs = <");
    for (size_t i = 0; i < len; i++) {
        shell_print(shell, "\t\t%d", entries[i].avg_high);
    }
    shell_print(shell, "\t>;");
    shell_print(shell, "precalib-avg-lows = <");
    for (size_t i = 0; i < len; i++) {
        shell_print(shell, "\t\t%d", entries[i].avg_low);
    }
    shell_print(shell, "\t>;");
}

int zmk_analog_matrix_cmd_calibration_export(const struct shell *shell, size_t argc, char **argv,
                                         void *data) {
    const struct device *dev = device_get_binding(argv[-2]);

    if (!dev) {
        shell_error(shell, "Failed to find device named %s", argv[-2]);
	return -ENODEV;
    }

    int ret = zmk_analog_matrix_access_calibration(dev, &export_cb, shell);
    if (ret < 0) {
        shell_print(shell, "Failed to access calibration data to export (%d)", ret);
    }

    return ret;
}

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SCAN_RATE_CALC)

int zmk_analog_matrix_cmd_scan_rate(const struct shell *shell, size_t argc, char **argv, void *data) {
    const struct device *dev = device_get_binding(argv[-1]);

    if (!dev) {
        shell_error(shell, "Failed to find device named %s", argv[-1]);
	return -ENODEV;
    }

    uint64_t duration_ns = zmk_analog_matrix_max_scan_duration_ns(dev);

    if (duration_ns > 0) {
        uint64_t scan_rate = 1000000000 / duration_ns;
        shell_info(shell, "Matrix scan rate: %lluHz", scan_rate);
    }

    return 0;
}

#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SCAN_RATE_CALC)

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SETTINGS)

int zmk_analog_matrix_cmd_calibration_save(const struct shell *shell, size_t argc, char **argv,
                                       void *data) {
    const struct device *dev = device_get_binding(argv[-2]);
    if (!dev) {
        shell_error(shell, "Failed to find device named %s", argv[-2]);
	return -ENODEV;
    }

    int ret = zmk_analog_matrix_settings_save_calibration(dev);
    if (ret < 0) {
        shell_print(shell, "Failed to initiate save calibration (%d)", ret);
    }

    return ret;
}

int zmk_analog_matrix_cmd_calibration_load(const struct shell *shell, size_t argc, char **argv,
                                       void *data) {
    const struct device *dev = device_get_binding(argv[-2]);
    if (!dev) {
        shell_error(shell, "Failed to find device named %s", argv[-2]);
	return -ENODEV;
    }

    int ret = zmk_analog_matrix_settings_load_calibration(dev);
    if (ret < 0) {
        shell_print(shell, "Failed to initiate load calibration (%d)", ret);
    }

    return ret;
}

#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SETTINGS)
