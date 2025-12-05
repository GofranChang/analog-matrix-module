/*
 * Copyright (c) 2025 Peter Johanson
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

int zmk_analog_matrix_cmd_calibration_start(const struct shell *shell, size_t argc, char **argv, void *data);
int zmk_analog_matrix_cmd_calibration_export(const struct shell *shell, size_t argc, char **argv, void *data);
int zmk_analog_matrix_cmd_calibration_save(const struct shell *shell, size_t argc, char **argv, void *data);
int zmk_analog_matrix_cmd_calibration_load(const struct shell *shell, size_t argc, char **argv, void *data);
int zmk_analog_matrix_cmd_sample(const struct shell *shell, size_t argc, char **argv, void *data);
int zmk_analog_matrix_cmd_scan_rate(const struct shell *shell, size_t argc, char **argv, void *data);

#define CMD_HELP_SCAN_RATE "Print Scan Rate.\n"
#define CMD_HELP_SAMPLE "Sample a specific position.\n"
#define CMD_HELP_CALIBRATE "Calibration Utilities.\n"
#define CMD_HELP_CALIBRATION_START "Calibrate the Martix.\n"
#define CMD_HELP_CALIBRATION_EXPORT "Export calibration data as DTS props.\n"

#define CMD_HELP_CALIBRATION_SAVE "Save the Martix Calibration To Flash.\n"

#define CMD_HELP_CALIBRATION_LOAD "Load the Martix Calibration From Flash.\n"

#define ANALOG_MATRIX_SHELL_CMDS(_name, _calibration_sub_name, ...) \
  SHELL_STATIC_SUBCMD_SET_CREATE( \
      _name, \
      SHELL_CMD(calibration, &_calibration_sub_name, CMD_HELP_CALIBRATE, NULL), \
  COND_CODE_1(IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_CALIBRATOR), \
      (SHELL_CMD_ARG(sample, NULL, CMD_HELP_SAMPLE, zmk_analog_matrix_cmd_sample, 2, 3),), ()) \
      COND_CODE_1(IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_SCAN_RATE_CALC), \
          (SHELL_CMD(scan_rate, NULL, CMD_HELP_SCAN_RATE, zmk_analog_matrix_cmd_scan_rate),), ()) \
  __VA_ARGS__ \
      SHELL_SUBCMD_SET_END /* Array terminated. */ \
  );
  
#define ANALOG_MATRIX_CALIBRATION_CMD_SET(_name) \
  SHELL_STATIC_SUBCMD_SET_CREATE( \
      _name, \
  COND_CODE_1(IS_ENABLED(CONFIG_ZMK_ANALOG_MATRIX_CALIBRATOR), \
      (SHELL_CMD(start, NULL, CMD_HELP_CALIBRATION_START, zmk_analog_matrix_cmd_calibration_start),), ()) \
      SHELL_CMD(export, NULL, CMD_HELP_CALIBRATION_EXPORT, zmk_analog_matrix_cmd_calibration_export), \
  COND_CODE_1(IS_ENABLED(CONFIG_SETTINGS), \
      (SHELL_CMD(save, NULL, CMD_HELP_CALIBRATION_SAVE, zmk_analog_matrix_cmd_calibration_save), \
      SHELL_CMD(load, NULL, CMD_HELP_CALIBRATION_LOAD, zmk_analog_matrix_cmd_calibration_load),), ()) \
      SHELL_SUBCMD_SET_END /* Array terminated. */ \
  );
