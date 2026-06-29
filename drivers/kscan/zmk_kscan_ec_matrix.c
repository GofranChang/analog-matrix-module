/*
 * Copyright (c) 2025 Peter Johanson
 * Copyright (c) 2022, 2023 Kan-Ru Chen
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_kscan_ec_matrix

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/kscan.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/util.h>

#include "zmk_analog_matrix.h"
#include "zmk_kscan_ec_matrix.h"

#define LOG_LEVEL CONFIG_KSCAN_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zmk_kscan_ec_matrix);

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
#include <zephyr/timing/timing.h>
#endif

struct kscan_ec_matrix_config {
    struct zmk_analog_matrix_common_cfg common;
    const struct pinctrl_dev_config *pcfg;
    struct gpio_dt_spec power;
    struct gpio_dt_spec drain;
    const struct adc_dt_spec adc_channel;
    const bool skip_startup_calibration;
    const uint16_t matrix_warm_up_us;
    const uint16_t matrix_relax_us;
    const uint16_t adc_read_settle_us;
    const struct gpio_dt_spec *selects;
    const struct gpio_dt_spec strobes[];
};

struct kscan_ec_matrix_data {
    struct zmk_analog_matrix_common_data common;
    K_KERNEL_STACK_MEMBER(thread_stack, CONFIG_ZMK_KSCAN_EC_MATRIX_THREAD_STACK_SIZE);
#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
    struct zmk_kscan_ec_matrix_read_timing read_timing;
#endif
};

static uint16_t read_raw_matrix_state(const struct device *dev, uint8_t select, uint8_t strobe) {
    const struct kscan_ec_matrix_config *cfg = dev->config;
    int ret;

    int16_t buf = 0;
    struct adc_sequence sequence = {
        .buffer = &buf,
        .buffer_size = sizeof(buf),
    };

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
    struct kscan_ec_matrix_data *data = dev->data;

    timing_start();
    timing_t start_time = timing_counter_get();
#endif

    adc_sequence_init_dt(&cfg->adc_channel, &sequence);

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
    timing_t adc_init_done = timing_counter_get();
#endif

    ret = gpio_pin_configure_dt(&cfg->selects[select], GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to set the select pin (%d)", ret);
    }

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
    timing_t gpio_input_done = timing_counter_get();
#endif

    // TODO: Only wait as long as is need after drain pin was set low.
    if (cfg->matrix_relax_us) {
        k_busy_wait(cfg->matrix_relax_us);
    }

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
    timing_t relax_done = timing_counter_get();
#endif

    const uint32_t lock = irq_lock();

    if (cfg->drain.port != NULL) {
#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_FAKE_OPEN_DRAIN)
        gpio_pin_configure_dt(&cfg->drain, GPIO_INPUT);
#else
        gpio_pin_set_dt(&cfg->drain, 1);
#endif
    }

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
    timing_t drain_released_done = timing_counter_get();
#endif

    gpio_pin_set_dt(&cfg->strobes[strobe], 1);

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
    timing_t set_strobe_done = timing_counter_get();
#endif

    k_busy_wait(0);
    if (cfg->adc_read_settle_us) {
        k_busy_wait(cfg->adc_read_settle_us);
    }

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
    timing_t adc_read_settle_done = timing_counter_get();
#endif

    ret = adc_read(cfg->adc_channel.dev, &sequence);
    if (ret < 0) {
        LOG_ERR("ADC READ ERROR %d", ret);
    }

    irq_unlock(lock);

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
    timing_t adc_read_done = timing_counter_get();
#endif

    gpio_pin_set_dt(&cfg->strobes[strobe], 0);

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
    timing_t strobe_unset_done = timing_counter_get();
#endif

    if (cfg->drain.port != NULL) {
#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_FAKE_OPEN_DRAIN)
        gpio_pin_configure_dt(&cfg->drain, GPIO_OUTPUT);
#endif // IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_FAKE_OPEN_DRAIN)
        gpio_pin_set_dt(&cfg->drain, 0);
    }

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
    timing_t drain_unset_done = timing_counter_get();
#endif

    gpio_pin_configure_dt(&cfg->selects[select], GPIO_DISCONNECTED);

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
    timing_t gpio_input_disconnect_done = timing_counter_get();

    timing_stop();

    data->read_timing = (struct zmk_kscan_ec_matrix_read_timing){
        .total_ns =
            timing_cycles_to_ns(timing_cycles_get(&start_time, &gpio_input_disconnect_done)),
        .adc_sequence_init_ns = timing_cycles_to_ns(timing_cycles_get(&start_time, &adc_init_done)),
        .gpio_input_ns = timing_cycles_to_ns(timing_cycles_get(&adc_init_done, &gpio_input_done)),
        .relax_ns = timing_cycles_to_ns(timing_cycles_get(&gpio_input_done, &relax_done)),
        .plug_drain_ns = timing_cycles_to_ns(timing_cycles_get(&relax_done, &drain_released_done)),
        .set_strobe_ns =
            timing_cycles_to_ns(timing_cycles_get(&drain_released_done, &set_strobe_done)),
        .read_settle_ns =
            timing_cycles_to_ns(timing_cycles_get(&set_strobe_done, &adc_read_settle_done)),
        .adc_read_ns =
            timing_cycles_to_ns(timing_cycles_get(&adc_read_settle_done, &adc_read_done)),
        .unset_strobe_ns =
            timing_cycles_to_ns(timing_cycles_get(&adc_read_done, &strobe_unset_done)),
        .pull_drain_ns =
            timing_cycles_to_ns(timing_cycles_get(&strobe_unset_done, &drain_unset_done)),
        .input_disconnect_ns =
            timing_cycles_to_ns(timing_cycles_get(&drain_unset_done, &gpio_input_disconnect_done)),
    };
#endif

    return buf;
}

static void scan_raw_values(const struct device *dev, zmk_analog_matrix_value_cb_t cb,
                            void *user_data) {
    const struct zmk_analog_matrix_common_cfg *common_cfg = dev->config;
    for (int sel = 0; sel < common_cfg->selects_len; sel++) {
        for (int str = 0; str < common_cfg->inputs_len; str++) {
            if (!zmk_analog_matrix_valid_sel_str(dev, sel, str, true)) {
                continue;
            }

            uint16_t raw = read_raw_matrix_state(dev, sel, str);
            cb(dev, sel, str, raw, user_data);
        }
    }
}

static void zmk_kscan_ec_matrix_power(const struct device *dev, bool enable) {
    const struct kscan_ec_matrix_config *cfg = dev->config;
    if (!cfg->power.port) {
        return;
    }

    if (enable) {
        gpio_pin_set_dt(&cfg->power, 1);
        k_busy_wait(cfg->matrix_warm_up_us);
    } else {
        gpio_pin_set_dt(&cfg->power, 0);
    }
}

static uint8_t zmk_kscan_ec_matrix_input_resolution(const struct device *dev, uint8_t input) {
    const struct kscan_ec_matrix_config *cfg = dev->config;
    return cfg->adc_channel.resolution;
}

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_CALIBRATOR)

int zmk_kscan_ec_matrix_calibrate(const struct device *dev,
                                  zmk_analog_matrix_calibration_cb_t callback,
                                  const void *user_data) {
    struct zmk_analog_matrix_common_data *common_data = dev->data;

    int ret = k_mutex_lock(&common_data->mutex, K_SECONDS(1));

    if (ret < 0) {
        return -EAGAIN;
    }

    common_data->calibration_callback = callback;
    common_data->calibration_user_data = user_data;

    k_mutex_unlock(&common_data->mutex);

    return 0;
}

int zmk_kscan_ec_matrix_sample(const struct device *dev, uint8_t select, uint8_t strobe,
                               uint16_t times, zmk_analog_matrix_sample_cb_t callback,
                               const void *user_data) {
    struct zmk_analog_matrix_common_data *common_data = dev->data;
    const struct zmk_analog_matrix_common_cfg *common_cfg = dev->config;

    if (strobe >= common_cfg->inputs_len || select >= common_cfg->selects_len) {
        return -EINVAL;
    }

    int ret = k_mutex_lock(&common_data->mutex, K_SECONDS(1));

    if (ret < 0) {
        return -EAGAIN;
    }

    common_data->sample_callback = callback;
    common_data->sample_user_data = user_data;
    common_data->sample_strobe = strobe;
    common_data->sample_select = select;
    common_data->sample_times = times;

    k_mutex_unlock(&common_data->mutex);

    return 0;
}

#endif // IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_CALIBRATOR)

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)

struct zmk_kscan_ec_matrix_read_timing zmk_kscan_ec_matrix_read_timing(const struct device *dev) {
    struct kscan_ec_matrix_data *data = dev->data;

    k_mutex_lock(&data->mutex, K_MSEC(10));

    struct zmk_kscan_ec_matrix_read_timing val = data->read_timing;

    k_mutex_unlock(&data->mutex);

    return val;
}

#endif // IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)

static int kscan_ec_matrix_init(const struct device *dev) {
    int err;
    struct kscan_ec_matrix_data *data = dev->data;
    struct zmk_analog_matrix_common_data *common_data = dev->data;
    const struct zmk_analog_matrix_common_cfg *common_cfg = dev->config;
    const struct kscan_ec_matrix_config *cfg = dev->config;

    err = zmk_analog_matrix_init(dev);
    if (err < 0) {
        LOG_ERR("Failed to do base analog matrix init (%d)", err);
        return err;
    }

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)
    timing_init();
#endif // IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)

    if (!device_is_ready(cfg->adc_channel.dev)) {
        LOG_ERR("ADC Channel device is not ready");
        return -ENODEV;
    }

    err = adc_channel_setup_dt(&cfg->adc_channel);
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

        adc_sequence_init_dt(&cfg->adc_channel, &sequence);
        sequence.calibrate = true;

        err = adc_read(cfg->adc_channel.dev, &sequence);
        if (err < 0) {
            LOG_ERR("Failed to calibrate on startup: %d", err);
            return err;
        }
    }

    if (cfg->pcfg) {
        err = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
        if (err < 0) {
            LOG_ERR("Failed to apply pinctrl state");
            return err;
        }
    }

    if (cfg->power.port != NULL) {
        if (!device_is_ready(cfg->power.port)) {
            LOG_ERR("Power port is not ready");
            return -ENODEV;
        }

        gpio_pin_configure_dt(&cfg->power, GPIO_OUTPUT_INACTIVE);
    }

    if (cfg->drain.port != NULL) {
        if (!device_is_ready(cfg->drain.port)) {
            LOG_ERR("Drain port is not ready");
            return -ENODEV;
        }

        gpio_pin_configure_dt(&cfg->drain, GPIO_OUTPUT_INACTIVE);
    }

    for (int str = 0; str < common_cfg->inputs_len; str++) {
        if (!device_is_ready(cfg->strobes[str].port)) {
            LOG_ERR("Strobe port is not ready");
            return -ENODEV;
        }

        gpio_pin_configure_dt(&cfg->strobes[str], GPIO_OUTPUT_INACTIVE);
    }

    for (int sel = 0; sel < common_cfg->selects_len; sel++) {
        if (!device_is_ready(cfg->selects[sel].port)) {
            LOG_ERR("Input port is not ready");
            return -ENODEV;
        }

        gpio_pin_configure_dt(&cfg->selects[sel], GPIO_DISCONNECTED);
    }

    k_thread_create(&common_data->thread, data->thread_stack,
                    CONFIG_ZMK_KSCAN_EC_MATRIX_THREAD_STACK_SIZE, zmk_analog_matrix_thread_main,
                    (void *)dev, NULL, NULL,
                    K_PRIO_COOP(CONFIG_ZMK_KSCAN_EC_MATRIX_THREAD_PRIORITY), 0, K_NO_WAIT);

    return 0;
}

static const struct kscan_driver_api kscan_ec_matrix_api = {
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

#define ZERO(n, idx) 0

#define ENTRIES(n) DT_INST_PROP_LEN(n, strobe_gpios) * DT_INST_PROP_LEN(n, input_gpios)

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
    static uint64_t reported_matrix_states_##n[DT_INST_PROP_LEN(n, strobe_gpios)] = {0};           \
    COND_CODE_1(                                                                                   \
        DT_INST_NODE_HAS_PROP(n, strobe_input_masks),                                              \
        (static const uint32_t input_select_masks_##n[] = DT_INST_PROP(n, strobe_input_masks);),   \
        ())                                                                                        \
    static uint64_t matrix_state_##n[] = {LISTIFY(DT_INST_PROP_LEN(n, strobe_gpios), ZERO, (, ))}; \
    static struct kscan_ec_matrix_data kscan_ec_matrix_data##n = {                                 \
        .common =                                                                                  \
            {                                                                                      \
                .calibrations = calibration_entries_##n,                                           \
                .reported_matrix_state = reported_matrix_states_##n,                               \
                .matrix_state = matrix_state_##n,                                                  \
            },                                                                                     \
    };                                                                                             \
    static const struct gpio_dt_spec selects_##n[] = {                                             \
        DT_FOREACH_PROP_ELEM(DT_DRV_INST(n), input_gpios, ZKEM_GPIO_DT_SPEC_ELEM)};                \
    BUILD_ASSERT(DT_INST_PROP(n, trigger_percentage) > 10 &&                                       \
                     DT_INST_PROP(n, trigger_percentage) < 90,                                     \
                 "trigger-percentage must be between 10 and 95");                                  \
    static const struct kscan_ec_matrix_config kscan_ec_matrix_config##n = {                       \
        .common = {.selects_len = DT_INST_PROP_LEN(n, input_gpios),                                \
                   .inputs_len = DT_INST_PROP_LEN(n, strobe_gpios),                                \
                   .trigger_percentage = DT_INST_PROP_OR(n, trigger_percentage, 50),               \
                   .read = read_raw_matrix_state,                                                  \
                   .scan = scan_raw_values,                                                        \
                   .power = zmk_kscan_ec_matrix_power,                                             \
                   .input_resolution = zmk_kscan_ec_matrix_input_resolution,                       \
                   COND_CODE_1(DT_INST_NODE_HAS_PROP(n, strobe_input_masks),                       \
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
        COND_CODE_1(DT_INST_NODE_HAS_PROP(n, pinctrl_names),                                       \
                    (.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n), ), ())                             \
            .adc_channel = ADC_DT_SPEC_INST_GET(n),                                                \
        .power = GPIO_DT_SPEC_INST_GET_OR(n, power_gpios, {0}),                                    \
        .drain = GPIO_DT_SPEC_INST_GET_OR(n, drain_gpios, {0}),                                    \
        .strobes = {DT_FOREACH_PROP_ELEM(DT_DRV_INST(n), strobe_gpios, ZKEM_GPIO_DT_SPEC_ELEM)},   \
        .selects = selects_##n,                                                                    \
        .matrix_warm_up_us = DT_INST_PROP_OR(n, matrix_warm_up_us, 0),                             \
        .matrix_relax_us = DT_INST_PROP_OR(n, matrix_relax_us, 0),                                 \
        .adc_read_settle_us = DT_INST_PROP_OR(n, adc_read_settle_us, 0),                           \
        .skip_startup_calibration = DT_INST_PROP_OR(n, skip_startup_calibration, false),           \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, kscan_ec_matrix_init, PM_DEVICE_DT_INST_GET(n),                       \
                          &kscan_ec_matrix_data##n, &kscan_ec_matrix_config##n, POST_KERNEL,       \
                          CONFIG_KSCAN_INIT_PRIORITY, &kscan_ec_matrix_api);

DT_INST_FOREACH_STATUS_OKAY(ZKEM_INIT)
