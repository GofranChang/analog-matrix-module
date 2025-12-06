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
#include "zmk_kscan_ec_matrix.h"

#define DT_DRV_COMPAT zmk_kscan_ec_matrix

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ec_matrix_shell);

#define CMD_HELP_READ_TIMING "Print EC Read Timing.\n"

#define DEVICES(n) DEVICE_DT_INST_GET(n),

#define INIT_MACRO() DT_INST_FOREACH_STATUS_OKAY(DEVICES) NULL

#define EC_MATRIX_ENTRY(dev_) {.dev = dev_}

/* This table size is = ADC devices count + 1 (NA). */
static struct matrix_hdl {
    const struct device *dev;
} matrix_hdl_list[] = {FOR_EACH(EC_MATRIX_ENTRY, (, ), INIT_MACRO())};

#if IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)

static void print_pct(const struct shell *shell, uint64_t total_ns, uint64_t subset_ns,
                      const char *label) {
    uint32_t pct = (subset_ns * 100) / total_ns;

    shell_print(shell, "%s: %u%%", label, pct);
}

static int cmd_matrix_read_timing(const struct shell *shell, size_t argc, char **argv, void *data) {
    const struct device *dev = device_get_binding(argv[-1]);
    if (!dev) {
        shell_error(shell, "Failed to find device named %s", argv[-1]);
        return -ENODEV;
    }

    struct zmk_kscan_ec_matrix_read_timing timing = zmk_kscan_ec_matrix_read_timing(dev);

    shell_print(shell, "Total time for a read: %lluns", timing.total_ns);
    print_pct(shell, timing.total_ns, timing.adc_sequence_init_ns, "Sequence Init");
    print_pct(shell, timing.total_ns, timing.gpio_input_ns, "GPIO Input");
    print_pct(shell, timing.total_ns, timing.relax_ns, "Relax");
    print_pct(shell, timing.total_ns, timing.plug_drain_ns, "Plug Drain");
    print_pct(shell, timing.total_ns, timing.set_strobe_ns, "Set Strobe");
    print_pct(shell, timing.total_ns, timing.read_settle_ns, "Read Settle");
    print_pct(shell, timing.total_ns, timing.adc_read_ns, "ADC Read");
    print_pct(shell, timing.total_ns, timing.unset_strobe_ns, "Unset Strobe");
    print_pct(shell, timing.total_ns, timing.pull_drain_ns, "Pull Drain");
    print_pct(shell, timing.total_ns, timing.input_disconnect_ns, "Disconnect Input");

    return 0;
}

#endif // IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING)

ANALOG_MATRIX_CALIBRATION_CMD_SET(sub_matrix_calibration_cmds)

ANALOG_MATRIX_SHELL_CMDS(sub_matrix_cmds, sub_matrix_calibration_cmds,
                         COND_CODE_1(IS_ENABLED(CONFIG_ZMK_KSCAN_EC_MATRIX_READ_TIMING),
                                     (SHELL_CMD(read_timing, NULL, CMD_HELP_READ_TIMING,
                                                cmd_matrix_read_timing), ),
                                     ()));

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
SHELL_DYNAMIC_CMD_CREATE(sub_ec_matrix_dev, cmd_matrix_dev_get);

SHELL_CMD_REGISTER(ec, &sub_ec_matrix_dev, "EC Matrix commands", NULL);
