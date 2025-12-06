#pragma once

#include "zmk_analog_matrix.h"

int zmk_kscan_ec_matrix_calibrate(const struct device *dev, zmk_analog_matrix_calibration_cb_t cb,
                                  const void *user_data);

int zmk_kscan_ec_matrix_sample(const struct device *dev, uint8_t select, uint8_t strobe,
                               uint16_t times, zmk_analog_matrix_sample_cb_t callback,
                               const void *user_data);

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_SCAN_RATE_CALC)

uint64_t zmk_kscan_ec_matrix_max_scan_duration_ns(const struct device *dev);

#endif // IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_SCAN_RATE_CALC)

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)

struct zmk_kscan_ec_matrix_read_timing {
    uint64_t total_ns;
    uint64_t adc_sequence_init_ns;
    uint64_t gpio_input_ns;
    uint64_t relax_ns;
    uint64_t plug_drain_ns;
    uint64_t set_strobe_ns;
    uint64_t read_settle_ns;
    uint64_t adc_read_ns;
    uint64_t unset_strobe_ns;
    uint64_t pull_drain_ns;
    uint64_t input_disconnect_ns;
};

struct zmk_kscan_ec_matrix_read_timing zmk_kscan_ec_matrix_read_timing(const struct device *dev);

#endif // IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
