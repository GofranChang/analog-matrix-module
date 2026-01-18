
#include <sys/types.h>
#include <zephyr/settings/settings.h>

#include "analog_matrix_settings.h"
#include "zmk_analog_matrix.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_LEVEL CONFIG_KSCAN_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(zmk_analog_matrix_settings);

#define MAX_SETTING_LEN 32

struct load_state {
    char setting_name[MAX_SETTING_LEN];
    struct zmk_analog_matrix_calibration_entry *entries;
    size_t len;
};

static int settings_load_cb(const char *key, size_t len, settings_read_cb read_cb, void *cb_arg,
                            void *param) {
    struct load_state *state = (struct load_state *)param;

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SETTINGS_DISCRETE)

    if (len != sizeof(struct zmk_analog_matrix_calibration_entry)) {
        LOG_WRN("Ignoring settings with incorrect size");
        return -EINVAL;
    }

    char *endptr;
    size_t entry_id = strtoul(key, &endptr, 10);
    if (endptr != key) {
        if (entry_id >= state->len) {
            LOG_WRN("Ignoring calibration for invalid index %d, skipping", entry_id);
            return 0;
        }
        ssize_t ret = read_cb(cb_arg, &state->entries[entry_id], len);
        if (ret < 0) {
            LOG_ERR("Failed to load the settings from flash");
            return ret;
        }
    }
#else
    ssize_t ret = read_cb(cb_arg, state->entries,
                          state->len * sizeof(struct zmk_analog_matrix_calibration_entry));
    if (ret < 0) {
        LOG_ERR("Failed to load the settings from flash");
    }

    return ret;
#endif
    return 0;
}

static void load_cb(const struct device *dev, struct zmk_analog_matrix_calibration_entry *entries,
                    size_t len, const void *user_data) {
    struct load_state state = (struct load_state){.entries = entries, .len = len};
    snprintf(state.setting_name, MAX_SETTING_LEN,
             "zmk/" CONFIG_ZMK_ANALOG_MATRIX_SETTINGS_NAME_PREFIX "/cal/%s", dev->name);
    LOG_DBG("Loading the subtree directly for %s", state.setting_name);
    settings_load_subtree_direct(state.setting_name, settings_load_cb, &state);
}

static void save_cb(const struct device *dev, struct zmk_analog_matrix_calibration_entry *entries,
                    size_t len, const void *user_data) {
    char setting_name[MAX_SETTING_LEN];

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SETTINGS_DISCRETE)
    for (size_t i = 0; i < len; i++) {
        snprintf(setting_name, MAX_SETTING_LEN,
                 "zmk/" CONFIG_ZMK_ANALOG_MATRIX_SETTINGS_NAME_PREFIX "/cal/%s/%d", dev->name, i);
        int ret = settings_save_one(setting_name, &entries[i],
                                    sizeof(struct zmk_analog_matrix_calibration_entry));
        if (ret != 0) {
            LOG_WRN("Failed to save the settings for %s: %d", setting_name, ret);
            break;
        }
    }
#else
    snprintf(setting_name, MAX_SETTING_LEN,
             "zmk/" CONFIG_ZMK_ANALOG_MATRIX_SETTINGS_NAME_PREFIX "/cal/%s", dev->name);

    int ret = settings_save_one(setting_name, entries,
                                len * sizeof(struct zmk_analog_matrix_calibration_entry));

    if (ret != 0) {
        LOG_WRN("Failed to save the settings for %s: %d", setting_name, ret);
    }
#endif
}

int zmk_analog_matrix_settings_load_calibration(const struct device *dev) {
    int ret = zmk_analog_matrix_access_calibration(dev, &load_cb, NULL);
    return ret;
}

int zmk_analog_matrix_settings_save_calibration(const struct device *dev) {
#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_VALUE_INVERSION)
    char setting_name[MAX_SETTING_LEN];
    snprintf(setting_name, MAX_SETTING_LEN,
             "zmk/" CONFIG_ZMK_ANALOG_MATRIX_SETTINGS_NAME_PREFIX "/invert/%s", dev->name);
    struct zmk_analog_matrix_common_data *data = dev->data;
    uint8_t invert = data->invert_values ? 1 : 0;
    int ret = settings_save_one(setting_name, &invert, sizeof(invert));
    if (ret != 0) {
        LOG_WRN("Failed to save the invert setting: %d", ret);
        return ret;
    }

#endif /* IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_VALUE_INVERSION) */

    return zmk_analog_matrix_access_calibration(dev, &save_cb, NULL);
}

struct settings_load_state {
    settings_read_cb read_cb;
    void *cb_arg;
    size_t size;
#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SETTINGS_DISCRETE)
    uint8_t calibration_index;
#endif
};

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SETTINGS_DISCRETE)

static void load_discrete_calibrations_cb(const struct device *dev,
                                          struct zmk_analog_matrix_calibration_entry *entries,
                                          size_t len, const void *user_data) {
    const struct settings_load_state *load = user_data;

    if (load->size != sizeof(struct zmk_analog_matrix_calibration_entry)) {
        LOG_WRN("Incorrect stored calibration size (stored: %d, expected: %d)", load->size,
                sizeof(struct zmk_analog_matrix_calibration_entry));
        return;
    }

    if (load->calibration_index >= len) {
        LOG_WRN("Ignoring calibration for invalid index %d, skipping", load->calibration_index);
        return;
    }
    ssize_t ret = load->read_cb(load->cb_arg, &entries[load->calibration_index], len);
    if (ret < 0) {
        LOG_ERR("Failed to load the settings from flash (%d)", ret);
        return;
    }
}

#else

static void load_combined_calibrations_cb(const struct device *dev,
                                          struct zmk_analog_matrix_calibration_entry *entries,
                                          size_t len, const void *user_data) {
    const struct settings_load_state *load = user_data;

    if (load->size != (sizeof(struct zmk_analog_matrix_calibration_entry) * len)) {
        LOG_WRN("Incorrect stored calibration size (stored: %d, expected: %d)", load->size,
                (sizeof(struct zmk_analog_matrix_calibration_entry) * len));
        return;
    }

    ssize_t ret = load->read_cb(load->cb_arg, entries,
                                len * sizeof(struct zmk_analog_matrix_calibration_entry));
    if (ret < 0) {
        LOG_ERR("Failed to load the settings from flash");
    }
}

#endif

static int analog_matrix_settings_set(const char *name, size_t size, settings_read_cb read_cb,
                                      void *cb_arg) {
    const char *next;

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_VALUE_INVERSION)
    if (settings_name_steq(name, "invert", &next) && next) {
        const char *rem;
        int name_len = settings_name_next(next, &rem);
        if (rem) {
            LOG_WRN("Extra string in invert settings name %s", name);
            return -EINVAL;
        }

        char dev_name[name_len + 1];

        memcpy(dev_name, next, name_len);
        dev_name[name_len] = '\0';

        const struct device *dev = device_get_binding(dev_name);
        if (!dev) {
            LOG_ERR("No device found for setting for %s", dev_name);
            return -EINVAL;
        }

        uint8_t val;
        if (size != sizeof(val)) {
            LOG_WRN("Ignoring invert setting that's not a single byte");
            return -EINVAL;
        }

        ssize_t ret = read_cb(cb_arg, &val, size);
        if (ret < 0) {
            LOG_ERR("Failed to load the settings from flash");
            return ret;
        }

        struct zmk_analog_matrix_common_data *data = dev->data;
        data->invert_values = val != 0;
    }
#endif /* IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_VALUE_INVERSION) */

#if IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SETTINGS_DISCRETE)
    if (settings_name_steq(name, "cal", &next) && next) {
        int name_len = settings_name_next(next, &next);
        if (!next) {
            LOG_WRN("Missing calibration index in settings name %s", name);
            return -EINVAL;
        }

        char dev_name[name_len + 1];

        memcpy(dev_name, next, name_len);
        dev_name[name_len] = '\0';

        const struct device *dev = device_get_binding(dev_name);

        if (!dev) {
            LOG_WRN("Unable to locate device %s for calibration loading", dev_name);
            return -ENODEV;
        }

        if (size != sizeof(struct zmk_analog_matrix_calibration_entry)) {
            LOG_WRN("Ignoring settings with incorrect size");
            return -EINVAL;
        }

        char *endptr;
        size_t entry_id = strtoul(next, &endptr, 10);
        if (endptr == next) {
            LOG_WRN("Invalid discrete setting key %s", name);
            return -EINVAL;
        }
        struct settings_load_state state = (struct settings_load_state){
            .read_cb = read_cb,
            .cb_arg = cb_arg,
            .size = size,
            .calibration_index = entry_id,
        };

        int ret = zmk_analog_matrix_access_calibration(dev, &load_discrete_calibrations_cb, &state);
        if (ret < 0) {
            LOG_WRN("Failed to access calibration (%d)", ret);
            return ret;
        }
    }
#else
    if (settings_name_steq(name, "cal", &next) && next) {
        const struct device *dev = device_get_binding(next);
        if (!dev) {
            LOG_WRN("Loading calibration for non-existing device %s", next);
            return -ENODEV;
        }

        struct settings_load_state state = (struct settings_load_state){
            .read_cb = read_cb,
            .cb_arg = cb_arg,
            .size = size,
        };

        int ret = zmk_analog_matrix_access_calibration(dev, &load_combined_calibrations_cb, &state);
        if (ret < 0) {
            LOG_WRN("Failed to access calibration (%d)", ret);
            return ret;
        }
    }
#endif

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(zmk_analog_matrix_settings_handler,
                               "zmk/" CONFIG_ZMK_ANALOG_MATRIX_SETTINGS_NAME_PREFIX, NULL,
                               analog_matrix_settings_set, NULL, NULL);
