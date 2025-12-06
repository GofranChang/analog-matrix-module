/*
 * Copyright (c) 2025 Peter Johanson
 *
 * SPDX-License-Identifier: MIT
 */

#include "zmk_analog_matrix.h"

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SCAN_RATE_CALC)
#include <zephyr/timing/timing.h>
#endif

#define LOG_LEVEL CONFIG_KSCAN_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zmk_analog_matrix);

int zmk_analog_matrix_configure(const struct device *dev, kscan_callback_t callback) {
    struct zmk_analog_matrix_common_data *data = dev->data;
    if (!callback) {
        return -EINVAL;
    }
    data->callback = callback;
    return 0;
}

int zmk_analog_matrix_enable(const struct device *dev) {
    const struct zmk_analog_matrix_common_cfg *cfg = dev->config;
    struct zmk_analog_matrix_common_data *common_data = dev->data;

    common_data->poll_interval = cfg->active_polling_interval_ms;

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)
    common_data->last_key_released_at = k_uptime_get();
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)

    k_thread_resume(&common_data->thread);

    return 0;
}

int zmk_analog_matrix_disable(const struct device *dev) {
    struct zmk_analog_matrix_common_data *common_data = dev->data;

    k_thread_suspend(&common_data->thread);

    return 0;
}

int zmk_analog_matrix_access_calibration(const struct device *dev,
                                         zmk_analog_matrix_calibration_access_cb_t cb,
                                         const void *user_data) {
    const struct zmk_analog_matrix_common_cfg *common_cfg = dev->config;
    struct zmk_analog_matrix_common_data *common_data = dev->data;

    int ret = k_mutex_lock(&common_data->mutex, K_SECONDS(1));

    if (ret < 0) {
        return -EAGAIN;
    }

    cb(dev, common_data->calibrations, common_cfg->selects_len * common_cfg->inputs_len, user_data);

    k_mutex_unlock(&common_data->mutex);

    return 0;
}

struct zmk_analog_matrix_calibration_entry *
zmk_analog_matrix_calibration_entry_for_sel_str(const struct device *dev, uint8_t select, uint8_t strobe) {
    struct zmk_analog_matrix_common_data *data = dev->data;
    const struct zmk_analog_matrix_common_cfg *common_cfg = dev->config;

    return &data->calibrations[(strobe * common_cfg->selects_len) + select];
}


bool zmk_analog_matrix_valid_sel_str(const struct device *dev, uint8_t sel, uint8_t str, bool need_calibration) {
    const struct zmk_analog_matrix_common_cfg *cfg = dev->config;

    if (cfg->input_select_masks && (cfg->input_select_masks[str] & BIT(sel)) != 0) {
        return false;
    }

    struct zmk_analog_matrix_calibration_entry *calibration =
        zmk_analog_matrix_calibration_entry_for_sel_str(dev, sel, str);
    if (need_calibration && (!calibration || calibration->avg_high == 0)) {
        return false;
    }

    return true;
};

static inline uint16_t normalize(uint16_t val, uint16_t avg_low, uint16_t avg_high) {
    val = MAX(val, avg_low);
    val = MIN(val, avg_high);

    uint32_t numerator = UINT16_MAX * (val - avg_low);
    uint16_t denominator = avg_high - avg_low;

    return (uint16_t)(numerator / denominator);
}

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)
static void analog_matrix_update_poll_interval(const struct device *dev) {
    struct zmk_analog_matrix_common_data *common_data = dev->data;
    const struct zmk_analog_matrix_common_cfg *cfg = dev->config;

    uint32_t last_released_at = common_data->last_key_released_at;
    uint32_t prev_poll_interval = common_data->poll_interval;
    uint32_t new_poll_interval = 0;

    if (last_released_at == 0) {
        new_poll_interval = cfg->active_polling_interval_ms;
    } else {
        uint32_t ms_since_last_released = k_uptime_get() - last_released_at;

        if (ms_since_last_released > cfg->sleep_after_secs * 1000) {
            new_poll_interval = cfg->sleep_polling_interval_ms;
        } else if (ms_since_last_released > cfg->idle_after_secs * 1000) {
            new_poll_interval = cfg->idle_polling_interval_ms;
        } else {
            new_poll_interval = cfg->active_polling_interval_ms;
        }
    }

    if (new_poll_interval != prev_poll_interval) {
        LOG_DBG("Poll interval: %d -> %d", prev_poll_interval, new_poll_interval);
        common_data->poll_interval = new_poll_interval;
    }
}
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_CALIBRATOR)

#define SAMPLE_COUNT 20

struct sample_results {
    uint16_t min;
    uint16_t max;
    uint16_t avg;
    uint16_t noise;
};

struct sample_results sample(const struct device *dev, uint8_t select, uint8_t strobe) {
    const struct zmk_analog_matrix_common_cfg *common_cfg = dev->config;
    uint16_t min = 0, max = 0, avg = 0;

    for (int sample = 0; sample < SAMPLE_COUNT; sample++) {
        uint16_t val = common_cfg->read(dev, select, strobe);

        if (sample == 0) {
            avg = min = max = val;
        } else {
            max = MAX(val, max);
            min = MIN(val, min);
            avg = ((avg * sample) + val) / (sample + 1);
        }

        k_sleep(K_MSEC(1));
    }

    return (struct sample_results){
        .min = min,
        .max = max,
        .avg = avg,
        .noise = max - min,
    };
}

static void run_sample(const struct device *dev) {
    const struct zmk_analog_matrix_common_cfg *common_cfg = dev->config;
    struct zmk_analog_matrix_common_data *common_data = dev->data;

    if (common_cfg->power) {
        common_cfg->power(dev, true);
    }

    for (int i = 0; i < common_data->sample_times; i++) {
            uint16_t buf = common_cfg->read(dev, common_data->sample_select, common_data->sample_strobe);
	    common_data->sample_callback(buf, common_data->sample_user_data);
	    k_sleep(K_SECONDS(1));
    }
    common_data->sample_callback = NULL;

    if (common_cfg->power) {
        common_cfg->power(dev, false);
    }
}

int zmk_analog_matrix_calibrate(const struct device *dev,
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

int zmk_analog_matrix_sample(const struct device *dev,
		uint8_t select,
		uint8_t input,
		uint16_t times,
                                  zmk_analog_matrix_sample_cb_t callback,
                                  const void *user_data) {
    struct zmk_analog_matrix_common_data *common_data = dev->data;
    const struct zmk_analog_matrix_common_cfg *common_cfg = dev->config;

    if (input >= common_cfg->inputs_len || select >= common_cfg->selects_len) {
	    return -EINVAL;
    }

    int ret = k_mutex_lock(&common_data->mutex, K_SECONDS(1));

    if (ret < 0) {
        return -EAGAIN;
    }

    common_data->sample_callback = callback;
    common_data->sample_user_data = user_data;
    common_data->sample_strobe = input;
    common_data->sample_select = select;
    common_data->sample_times = times;

    k_mutex_unlock(&common_data->mutex);

    return 0;
}

static void calibrate(const struct device *dev) {
    const struct zmk_analog_matrix_common_cfg *common_cfg = dev->config;
    struct zmk_analog_matrix_common_data *common_data = dev->data;
    uint16_t keys_to_complete = 0;
    if (common_data->calibration_callback) {
        struct zmk_analog_matrix_calibration_event ev = {
            .type = CALIBRATION_EV_LOW_SAMPLING_START, .data = {}};
        common_data->calibration_callback(&ev, common_data->calibration_user_data);
    }

    if (common_cfg->power) {
        common_cfg->power(dev, true);
    }

    // Read one sample and toss it. This ensures the ADC has been enabled before taking real
    // samples.
    common_cfg->read(dev, 0, 0);

    for (int sel = 0; sel < common_cfg->selects_len; sel++) {
        for (int str = 0; str < common_cfg->inputs_len; str++) {
	    if (!zmk_analog_matrix_valid_sel_str(dev, sel, str, false)) {
                continue;
	    }

            struct zmk_analog_matrix_calibration_entry *calibration =
                zmk_analog_matrix_calibration_entry_for_sel_str(dev, sel, str);
            memset(calibration, 0, sizeof(struct zmk_analog_matrix_calibration_entry));
            struct sample_results low_res = sample(dev, sel, str);

            LOG_DBG("Low avg for %d,%d using %d and %d is %d. Noise %d", str, sel, low_res.max,
                    low_res.min, low_res.avg, low_res.noise);
            if (common_data->calibration_callback) {
                struct zmk_analog_matrix_calibration_event ev = {
                    .type = CALIBRATION_EV_POSITION_LOW_DETERMINED,
                    .data = {.position_low_determined = {.low_avg = low_res.avg,
                                                         .input = str,
                                                         .select = sel,
                                                         .noise = low_res.noise}}};
                common_data->calibration_callback(&ev, common_data->calibration_user_data);
            }

            calibration->avg_low = low_res.avg;
            calibration->noise = low_res.noise;
            keys_to_complete++;
        }
    }

    if (common_data->calibration_callback) {
        struct zmk_analog_matrix_calibration_event ev = {
            .type = CALIBRATION_EV_HIGH_SAMPLING_START, .data = {}};
        common_data->calibration_callback(&ev, common_data->calibration_user_data);
    }

    while (keys_to_complete > 0) {
        for (int sel = 0; sel < common_cfg->selects_len; sel++) {
            for (int str = 0; str < common_cfg->inputs_len; str++) {
	        if (!zmk_analog_matrix_valid_sel_str(dev, sel, str, false)) {
                    continue;
	        }

                struct zmk_analog_matrix_calibration_entry *calibration =
                    zmk_analog_matrix_calibration_entry_for_sel_str(dev, sel, str);

                if (calibration->avg_high > 0) {
                    continue;
                }

		uint16_t high_threshold;
		if (common_cfg->high_threshold_noise_based) {
			high_threshold = calibration->avg_low + (common_cfg->high_threshold_noise_mult * MAX(calibration->noise, 1));
		} else {
			high_threshold = calibration->avg_low + (((1 << (common_cfg->input_resolution(dev, str) - 1)) / 4));
                	// high_threshold = (1 << (cfg->adc_channel.resolution - 1));
		}

                uint16_t high_check_val = common_cfg->read(dev, sel, str);

                if (high_check_val < high_threshold) {
                    continue;
                }

                k_sleep(K_MSEC(1));

                // Double checks to filter funky random one-off spikes
                high_check_val = common_cfg->read(dev, sel, str);

                if (high_check_val < high_threshold) {
                    continue;
                }

                LOG_DBG("Getting high for %d/%d after %d is higher than threashold: %d for "
                        "resolution %d",
                        str, sel, high_check_val, high_threshold, common_cfg->input_resolution(dev, str));
                k_sleep(K_MSEC(200));

                struct sample_results high_res = sample(dev, sel, str);

                // Rough approximation of SNR by using avg difference + noise over noise
                uint16_t snr =
                    (high_res.avg - calibration->avg_low + calibration->noise) / MAX(calibration->noise, 1);
                LOG_DBG("High avg for %d,%d is %d. SNR %d", str, sel, high_res.avg, snr);

                calibration->avg_high = high_res.avg;
                calibration->noise = MAX(calibration->noise, high_res.noise);
                keys_to_complete--;

                if (common_data->calibration_callback) {
                    struct zmk_analog_matrix_calibration_event ev = {
                        .type = CALIBRATION_EV_POSITION_COMPLETE,
                        .data = {.position_complete = {.high_avg = calibration->avg_high,
                                                       .snr = snr,
                                                       .low_avg = calibration->avg_low,
                                                       .input = str,
                                                       .select = sel,
                                                       .noise = calibration->noise}}};
                    common_data->calibration_callback(&ev, common_data->calibration_user_data);
                }

                k_sleep(K_MSEC(1));
            }

            k_sleep(K_MSEC(1));
        }

        k_sleep(K_MSEC(1));
    }

    if (common_cfg->power) {
        common_cfg->power(dev, false);
    }

    if (common_data->calibration_callback) {
        struct zmk_analog_matrix_calibration_event ev = {
            .type = CALIBRATION_EV_COMPLETE,
        };
        common_data->calibration_callback(&ev, common_data->calibration_user_data);
    }

    common_data->calibration_callback = NULL;
    common_data->calibration_user_data = NULL;
}

#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_CALIBRATOR)

void analog_matrix_read_cb(const struct device *dev, uint8_t sel, uint8_t str, uint16_t val, void *user_data) {
    const struct zmk_analog_matrix_common_cfg *cfg = dev->config;
    struct zmk_analog_matrix_common_data *common_data = dev->data;

    uint64_t *rows = (uint64_t *)user_data;
    struct zmk_analog_matrix_calibration_entry *calibration =
        zmk_analog_matrix_calibration_entry_for_sel_str(dev, sel, str);

    bool prev = (common_data->matrix_state[str] & BIT(sel)) != 0;

    val = normalize(val, calibration->avg_low, calibration->avg_high);

    uint32_t range = calibration->avg_high - calibration->avg_low;
    uint16_t press_limit_raw =
        calibration->avg_high -
        (uint16_t)(MAX((range * cfg->trigger_percentage) / 100, calibration->noise));
    uint16_t hys_buffer = MAX(range / 8, calibration->noise);
    uint16_t press_limit =
        normalize(press_limit_raw, calibration->avg_low, calibration->avg_high);
    uint16_t release_limit = normalize(press_limit_raw - hys_buffer, calibration->avg_low,
                                       calibration->avg_high);

    if (val > press_limit && !prev) {
        LOG_DBG("%d,%d is active with %d versus %d - %d", str, sel, val, release_limit, press_limit);
        WRITE_BIT(rows[str], sel, 1);
    } else if (prev && val < release_limit) {
        LOG_DBG("%d,%d is released with %d versus %d - %d", str, sel, val, release_limit, press_limit);
        WRITE_BIT(rows[str], sel, 0);
    } else {
        WRITE_BIT(rows[str], sel, prev);
    }
}

static void analog_matrix_read(const struct device *dev) {
    const struct zmk_analog_matrix_common_cfg *common_cfg = dev->config;
    struct zmk_analog_matrix_common_data *common_data = dev->data;

    uint64_t rows[common_cfg->inputs_len];

    for (int s = 0; s < common_cfg->inputs_len; s++) {
        rows[s] = 0;
    }

    if (common_cfg->power) {
        common_cfg->power(dev, true);
    }

    common_cfg->scan(dev, analog_matrix_read_cb, rows);

    if (common_cfg->power) {
        common_cfg->power(dev, false);
    }

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)
    bool have_change = false;
    bool have_keys = false;
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)

    uint64_t diffs[common_cfg->inputs_len];
    for (int s = 0; s < common_cfg->inputs_len; s++) {
        diffs[s] = rows[s] & common_data->matrix_state[s];
        if (rows[s] && rows[s] != common_data->matrix_state[s]) {
            LOG_DBG("Initial press detected for %d/%lld", s, rows[s] ^ common_data->matrix_state[s]);
        }
        common_data->matrix_state[s] = rows[s];
    }

    for (int s = 0; s < common_cfg->inputs_len; s++) {
        uint64_t diff = diffs[s];
        for (int r = 0; r < common_cfg->selects_len; r++) {
            if ((common_data->reported_matrix_state[s] & BIT(r)) != (diff & BIT(r))) {
#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)
                have_change = true;
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)

                LOG_DBG("Reporting %d/%d as %s", s, r, (diff & BIT(r)) ? "on" : "off");
                if (common_data->callback) {
                    common_data->callback(dev, s, r, diff & BIT(r));
                }
            } else if ((rows[s] & BIT(r)) &&
                       (common_data->reported_matrix_state[s] & BIT(r)) != (rows[s] & BIT(r))) {
                LOG_DBG("Bit enabled but not reporting yet %d/%d", s, r);
            }
        }

        common_data->reported_matrix_state[s] = diff;

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)
        have_keys = have_keys || diff != 0;
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)
    }

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)
    if (have_change) {
        if (have_keys) {
            common_data->last_key_released_at = 0;
        } else {
            common_data->last_key_released_at = k_uptime_get();
        }
    }
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)
}



void zmk_analog_matrix_thread_main(void *arg1, void *unused1, void *unused2) {
    ARG_UNUSED(unused1);
    ARG_UNUSED(unused2);

    const struct device *dev = (const struct device *)arg1;
    struct zmk_analog_matrix_common_data *common_data = dev->data;

    while (1) {
        k_mutex_lock(&common_data->mutex, K_FOREVER);

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_CALIBRATOR)
        if (common_data->calibration_callback || common_data->sample_callback) {
            if (common_data->calibration_callback) {
                calibrate(dev);
            } else if (common_data->sample_callback) {
                run_sample(dev);
            }
#else
        if (false) {
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_CALIBRATOR)

        } else {
#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SCAN_RATE_CALC)
            timing_start();
            timing_t c1 = timing_counter_get();
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SCAN_RATE_CALC)

            analog_matrix_read(dev);

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)
            const struct zmk_analog_matrix_common_cfg *common_cfg = dev->config;
            if (common_cfg->dynamic_polling_interval) {
                analog_matrix_update_poll_interval(dev);
            }
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SCAN_RATE_CALC)
            timing_t c2 = timing_counter_get();
            uint64_t cycles = timing_cycles_get(&c1, &c2);
            uint64_t ns_spent = timing_cycles_to_ns(cycles);
            timing_stop();

            common_data->max_scan_duration_ns = ns_spent;
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SCAN_RATE_CALC)
        }
        k_mutex_unlock(&common_data->mutex);

	if (common_data->poll_interval) {
		k_sleep(K_MSEC(common_data->poll_interval));
	} else {
		k_sleep(K_USEC(1));
	}
    }
}

int zmk_analog_matrix_init(const struct device *dev) {
    struct zmk_analog_matrix_common_data *common_data = dev->data;
    const struct zmk_analog_matrix_common_cfg *common_cfg = dev->config;

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)
    common_data->last_key_released_at = k_uptime_get();
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE)

    k_mutex_init(&common_data->mutex);

    common_data->poll_interval = common_cfg->active_polling_interval_ms;

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SCAN_RATE_CALC)
    timing_init();
#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SCAN_RATE_CALC)

    return 0;
}

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SCAN_RATE_CALC)

uint64_t zmk_analog_matrix_max_scan_duration_ns(const struct device *dev) {
    struct zmk_analog_matrix_common_data *data = dev->data;

    k_mutex_lock(&data->mutex, K_MSEC(10));

    uint64_t val = data->max_scan_duration_ns;

    k_mutex_unlock(&data->mutex);

    return val;
}

#endif // IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SCAN_RATE_CALC)
