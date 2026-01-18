/*
 * Copyright (c) 2025 Peter Johanson
 * Copyright (c) 2022, 2023 Kan-Ru Chen
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_kscan_he_matrix

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/util.h>

#include "zmk_analog_matrix.h"
// #include "zmk_kscan_ec_matrix.h"

#define LOG_LEVEL CONFIG_KSCAN_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zmk_kscan_he_matrix);

struct kscan_he_matrix_config {
    struct zmk_analog_matrix_common_cfg common;
    const bool skip_startup_calibration;
    const struct gpio_dt_spec *selects;
    const struct adc_dt_spec channels[];
};

struct kscan_he_matrix_data {
    struct zmk_analog_matrix_common_data common;
    K_KERNEL_STACK_MEMBER(thread_stack, CONFIG_ZMK_KSCAN_HE_MATRIX_THREAD_STACK_SIZE);
};

static int read_channels(const struct device *dev, uint8_t select,
                         const struct adc_dt_spec *channels, size_t channels_len, uint16_t *buf) {
    const struct kscan_he_matrix_config *cfg = dev->config;
    int ret;

    struct adc_sequence sequence = {
        .buffer = buf,
        .buffer_size = channels_len * sizeof(uint16_t),
        .resolution = channels[0].resolution,
    };

    for (size_t c = 0; c < channels_len; c++) {
        WRITE_BIT(sequence.channels, channels[c].channel_id, true);
    }

    ret = gpio_pin_configure_dt(&cfg->selects[select], GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to set the select pin (%d)", ret);
        return ret;
    }

    ret = adc_read(channels[0].dev, &sequence);
    if (ret < 0) {
        LOG_ERR("ADC READ ERROR %d", ret);
        return ret;
    }

    gpio_pin_configure_dt(&cfg->selects[select], GPIO_DISCONNECTED);

    return 0;
}

static inline uint8_t input_resolution(const struct device *dev, uint8_t input) {
    const struct kscan_he_matrix_config *cfg = dev->config;
    if (input >= cfg->common.inputs_len) {
        return 0;
    }

    return cfg->channels[input].resolution;
}

static uint8_t zmk_kscan_he_matrix_input_resolution(const struct device *dev, uint8_t input) {
    return input_resolution(dev, input);
}

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_VALUE_INVERSION)

static inline void invert_if_needed(const struct device *dev, uint16_t *val, uint8_t channel) {
    struct zmk_analog_matrix_common_data *common_data = dev->data;
    if (common_data->invert_values) {
        uint8_t res = zmk_kscan_he_matrix_input_resolution(dev, channel);

        ZMK_ANALOG_MATRIX_INVERT_VAL(val, res);
    }
}

#endif /* IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_VALUE_INVERSION) */

static uint16_t read_raw_matrix_state(const struct device *dev, uint8_t select, uint8_t channel) {
    const struct kscan_he_matrix_config *cfg = dev->config;
    int ret;

    int16_t buf = 0;
    struct adc_sequence sequence = {
        .buffer = &buf,
        .buffer_size = sizeof(buf),
    };

    adc_sequence_init_dt(&cfg->channels[channel], &sequence);

    ret = gpio_pin_configure_dt(&cfg->selects[select], GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to set the select pin (%d)", ret);
    }

    ret = adc_read(cfg->channels[channel].dev, &sequence);
    if (ret < 0) {
        LOG_ERR("ADC READ ERROR %d", ret);
    }

    gpio_pin_configure_dt(&cfg->selects[select], GPIO_DISCONNECTED);

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_VALUE_INVERSION)
    invert_if_needed(dev, &buf, channel);
#endif

    return buf;
}

static void scan_raw_values(const struct device *dev, zmk_analog_matrix_value_cb_t cb,
                            void *user_data) {
    const struct zmk_analog_matrix_common_cfg *common_cfg = dev->config;
    const struct kscan_he_matrix_config *cfg = dev->config;

    for (int sel = 0; sel < common_cfg->selects_len; sel++) {
        uint16_t buf[common_cfg->inputs_len];

        int ret = read_channels(dev, sel, cfg->channels, common_cfg->inputs_len, buf);
        if (ret < 0) {
            LOG_ERR("Failed to read the channels");
            continue;
        }

        for (int str = 0; str < common_cfg->inputs_len; str++) {
            if (!zmk_analog_matrix_valid_sel_str(dev, sel, str, true)) {
                continue;
            }

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_VALUE_INVERSION)
            invert_if_needed(dev, &buf[str], str);
#endif

            cb(dev, sel, str, buf[str], user_data);
        }
    }
}

static int kscan_he_matrix_init(const struct device *dev) {
    int err;
    struct kscan_he_matrix_data *data = dev->data;
    struct zmk_analog_matrix_common_data *common_data = dev->data;
    const struct zmk_analog_matrix_common_cfg *common_cfg = dev->config;
    const struct kscan_he_matrix_config *cfg = dev->config;

    err = zmk_analog_matrix_init(dev);
    if (err < 0) {
        LOG_ERR("Failed to do base analog matrix init (%d)", err);
        return err;
    }

    for (int i = 0; i < common_cfg->inputs_len; i++) {
        if (!device_is_ready(cfg->channels[i].dev)) {
            LOG_ERR("ADC Channel device is not ready");
            return -ENODEV;
        }

        err = adc_channel_setup_dt(&cfg->channels[i]);
        if (err < 0) {
            LOG_ERR("Failed to set up ADC channnel (%d)", err);
            return err;
        }

        if (!cfg->skip_startup_calibration) {
            int16_t buf = 0;
            struct adc_sequence sequence = {
                .buffer = &buf,
                .buffer_size = sizeof(buf),
            };

            adc_sequence_init_dt(&cfg->channels[i], &sequence);
            sequence.calibrate = true;

            err = adc_read(cfg->channels[i].dev, &sequence);
            if (err < 0) {
                LOG_ERR("Failed to calibrate on startup: %d", err);
                return err;
            }
        }
    }

    for (int sel = 0; sel < common_cfg->selects_len; sel++) {
        if (!device_is_ready(cfg->selects[sel].port)) {
            LOG_ERR("Input port is not ready");
            return -ENODEV;
        }

        gpio_pin_configure_dt(&cfg->selects[sel], GPIO_DISCONNECTED);
    }

    k_thread_create(&common_data->thread, data->thread_stack,
                    CONFIG_ZMK_KSCAN_HE_MATRIX_THREAD_STACK_SIZE, zmk_analog_matrix_thread_main,
                    (void *)dev, NULL, NULL,
                    K_PRIO_COOP(CONFIG_ZMK_KSCAN_HE_MATRIX_THREAD_PRIORITY), 0, K_MSEC(1));

    k_thread_suspend(&common_data->thread);
    return 0;
}

static const struct kscan_driver_api kscan_he_matrix_api = {
    .config = zmk_analog_matrix_configure,
    .enable_callback = zmk_analog_matrix_enable,
    .disable_callback = zmk_analog_matrix_disable,
};

#if IS_ENABLED(CONFIG_PM_DEVICE)

static int zkem_pm_resume(const struct device *dev) { return zmk_analog_matrix_enable(dev); }

static int zkem_pm_suspend(const struct device *dev) { return zmk_analog_matrix_disable(dev); }

static int zkem_pm_action(const struct device *dev, enum pm_device_action action) {
    switch (action) {
    case PM_DEVICE_ACTION_SUSPEND:
        return zkem_pm_suspend(dev);
    case PM_DEVICE_ACTION_RESUME:
        return zkem_pm_resume(dev);
    default:
        return -ENOTSUP;
    }
}

#endif //  IS_ENABLED(CONFIG_PM_DEVICE)

#define ZKEM_GPIO_DT_SPEC_ELEM(n, prop, idx) GPIO_DT_SPEC_GET_BY_IDX(n, prop, idx),
#define ZKHM_ADC_DT_SPEC_ELEM(n, prop, idx) ADC_DT_SPEC_GET_BY_IDX(n, idx),

#define ZERO(n, idx) 0

#define ENTRIES(n) DT_INST_PROP_LEN(n, io_channels) * DT_INST_PROP_LEN(n, select_gpios)

#define FOREACH_STROBE_CALIB_ENTRY(n, prop, idx)                                                   \
    {.avg_low = DT_PROP_BY_IDX(n, precalib_avg_lows, idx),                                         \
     .avg_high = DT_PROP_BY_IDX(n, precalib_avg_highs, idx)}

#define ZKEM_INIT(n)                                                                               \
    PM_DEVICE_DT_INST_DEFINE(n, zkem_pm_action);                                                   \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(n, pinctrl_names), (PINCTRL_DT_INST_DEFINE(n);), ())         \
    static struct zmk_analog_matrix_calibration_entry calibration_entries_##n[ENTRIES(n)] = {      \
        COND_CODE_1(DT_INST_NODE_HAS_PROP(n, precalib_avg_lows),                                   \
                    (DT_INST_FOREACH_PROP_ELEM_SEP(n, precalib_avg_lows,                           \
                                                   FOREACH_STROBE_CALIB_ENTRY, (, ))),             \
                    (0))};                                                                         \
    static uint64_t reported_matrix_states_##n[DT_INST_PROP_LEN(n, io_channels)] = {0};            \
    COND_CODE_1(                                                                                   \
        DT_INST_NODE_HAS_PROP(n, channel_select_masks),                                            \
        (static const uint32_t input_select_masks_##n[] = DT_INST_PROP(n, channel_select_masks);), \
        ())                                                                                        \
    static uint64_t matrix_state_##n[] = {LISTIFY(DT_INST_PROP_LEN(n, io_channels), ZERO, (, ))};  \
    static struct kscan_he_matrix_data kscan_he_matrix_data##n = {                                 \
        .common =                                                                                  \
            {                                                                                      \
                .calibrations = calibration_entries_##n,                                           \
                .reported_matrix_state = reported_matrix_states_##n,                               \
                .matrix_state = matrix_state_##n,                                                  \
            },                                                                                     \
    };                                                                                             \
    static const struct gpio_dt_spec selects_##n[] = {                                             \
        DT_FOREACH_PROP_ELEM(DT_DRV_INST(n), select_gpios, ZKEM_GPIO_DT_SPEC_ELEM)};               \
    BUILD_ASSERT(DT_INST_PROP(n, trigger_percentage) > 10 &&                                       \
                     DT_INST_PROP(n, trigger_percentage) < 90,                                     \
                 "trigger-percentage must be between 10 and 95");                                  \
    static const struct kscan_he_matrix_config kscan_he_matrix_config##n = {                       \
        .common = {.selects_len = DT_INST_PROP_LEN(n, select_gpios),                               \
                   .inputs_len = DT_INST_PROP_LEN(n, io_channels),                                 \
                   .trigger_percentage = DT_INST_PROP_OR(n, trigger_percentage, 50),               \
                   .high_threshold_noise_based = true,                                             \
                   .high_threshold_noise_mult = DT_INST_PROP_OR(n, high_threshold_noise_mult, 4),  \
                   .read = read_raw_matrix_state,                                                  \
                   .scan = scan_raw_values,                                                        \
                   .input_resolution = zmk_kscan_he_matrix_input_resolution,                       \
                   COND_CODE_1(DT_INST_NODE_HAS_PROP(n, channel_select_masks),                     \
                               (.input_select_masks = input_select_masks_##n, ), ())               \
                       .active_polling_interval_ms =                                               \
                       DT_INST_PROP_OR(n, active_polling_interval_ms, 1),                          \
                   COND_CODE_1(IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_DYNAMIC_POLL_RATE),           \
                               (.idle_polling_interval_ms =                                        \
                                    DT_INST_PROP_OR(n, idle_polling_interval_ms, 5),               \
                                .sleep_polling_interval_ms =                                       \
                                    DT_INST_PROP_OR(n, sleep_polling_interval_ms, 500),            \
                                .idle_after_secs = DT_INST_PROP_OR(n, idle_after_secs, 5),         \
                                .sleep_after_secs = DT_INST_PROP_OR(n, sleep_after_secs, 300),     \
                                .dynamic_polling_interval =                                        \
                                    DT_INST_PROP_OR(n, dynamic_polling_interval, false), ),        \
                               ())},                                                               \
        .channels = {DT_FOREACH_PROP_ELEM(DT_DRV_INST(n), io_channels, ZKHM_ADC_DT_SPEC_ELEM)},    \
        .selects = selects_##n,                                                                    \
        .skip_startup_calibration = DT_INST_PROP_OR(n, skip_startup_calibration, false),           \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, kscan_he_matrix_init, PM_DEVICE_DT_INST_GET(n),                       \
                          &kscan_he_matrix_data##n, &kscan_he_matrix_config##n, POST_KERNEL,       \
                          CONFIG_KSCAN_INIT_PRIORITY, &kscan_he_matrix_api);

DT_INST_FOREACH_STATUS_OKAY(ZKEM_INIT)
