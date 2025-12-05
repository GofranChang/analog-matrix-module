/*
 * Copyright (c) 2018 Prevas A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#include "analog_matrix_settings.h"
#include "analog_matrix_shell.h"

#define DT_DRV_COMPAT zmk_kscan_he_matrix

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(he_matrix_shell);

#define DEVICES(n) DEVICE_DT_INST_GET(n),

#define INIT_MACRO() DT_INST_FOREACH_STATUS_OKAY(DEVICES) NULL

#define HE_MATRIX_ENTRY(dev_) {.dev = dev_}

/* This table size is = ADC devices count + 1 (NA). */
static struct matrix_hdl {
    const struct device *dev;
} matrix_hdl_list[] = {FOR_EACH(HE_MATRIX_ENTRY, (, ), INIT_MACRO())};

ANALOG_MATRIX_CALIBRATION_CMD_SET(sub_matrix_calibration_cmds)

ANALOG_MATRIX_SHELL_CMDS(sub_matrix_cmds, sub_matrix_calibration_cmds);

static void cmd_matrix_dev_get(size_t idx, struct shell_static_entry *entry) {
    /* -1 because the last element in the list is a "list terminator" */
    if (idx < ARRAY_SIZE(matrix_hdl_list) - 1) {
        entry->syntax = matrix_hdl_list[idx].dev->name;
        entry->handler = NULL;
        entry->subcmd = &sub_matrix_cmds;
        entry->help = "Select subcommand for matrix property label.\n";
    } else {
        entry->syntax = NULL;
    }
}
SHELL_DYNAMIC_CMD_CREATE(sub_he_matrix_dev, cmd_matrix_dev_get);

SHELL_CMD_REGISTER(he, &sub_he_matrix_dev, "HE Matrix commands", NULL);
