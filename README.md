# Zephyr/ZMK Analog Matrix Module

This module is an evolution of the [ec-support-zmk-module](https://github.com/petejohanson/ec-support-zmk-module) that has been refactored to add support for HE PCBs.

Although HE support is considered experimental, it has been successfully validated on 3 different wired designs.

The drivers implement the now deprecated/removed KSCAN Zephyr API, making this driver only compatible with Zephyr 4.1 and older.

## EC Support

EC support has been well tested on wired and wireless designs. More documentation and links to refernece ZMK boards will be provided in the future. 

### Hardware

The driver assumes the standard PCB design using multiple strobe lines, and read lines all routed to a single opamp/ADC input by one or more analog muxes.

#### Unsupported

The previous EC support module included support for ADXL362 accelerometers for waking an EC device from deep sleep. That functionality is not yet available in this module, but is planned.

An example refernce design compatible with this driver can be found at https://github.com/sporkus/le_capybara_keyboard/

## HE Support

Initial basic HE support is available. More documentation and reference designs and board definitions will be provided once available.

### Hardware

HE support requires the fairly standard apporach of multiplexed sensors, routing the selected set of sensors to multiple ADC pins on the MCU. So far, this has been tested only in wired setups, wih STM32F4 and STM32G0 MCUs. Any future wireless PCBs will likely require additional design consideration, and driver enhancements in order to be as power efficient as possible.

No reference designs are yet available as open source hardware. Once any such designs are available, they will be linked here.

## Calibration

TODO: Add documentation on how to calibrate using the shell commands, and persist the changes.
