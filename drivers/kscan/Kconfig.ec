
config ZMK_KSCAN_EC_MATRIX
    bool "EC Matrix KScan Driver"
    depends on DT_HAS_ZMK_KSCAN_EC_MATRIX_ENABLED
    select ZMK_ANALOG_MATRIX
    default y

if ZMK_KSCAN_EC_MATRIX

config ZMK_ANALOG_MATRIX_SETTINGS_NAME_PREFIX
	default "ec"

config ZMK_KSCAN_EC_MATRIX_SHELL
	bool "EC Matrix Shell"
	default y
	select ZMK_ANALOG_MATRIX_SHELL
	depends on SHELL

config ZMK_KSCAN_EC_MATRIX_SETTINGS
	bool "EC Matrix Settings Storage [DEPRECATED]"
	select DEPRECATED
	depends on ZMK_ANALOG_MATRIX_SETTINGS

config ZMK_KSCAN_EC_MATRIX_SETTINGS_DISCRETE
	bool "Store individual calibration entries as disrete settings [DEPRECATED]"
	select DEPRECATED
	select ZMK_ANALOG_MATRIX_SETTINGS_DISCRETE
	depends on ZMK_ANALOG_MATRIX_SETTINGS

config ZMK_KSCAN_EC_MATRIX_DYNAMIC_POLL_RATE
	bool "Dynamic poll rate for power savings [DEPRECATED]"
	select DEPRECATED
	select ZMK_ANALOG_MATRIX_DYNAMIC_POLL_RATE

config ZMK_KSCAN_EC_MATRIX_FAKE_OPEN_DRAIN
	bool "Simulate open-drain config with input/output"

config ZMK_KSCAN_EC_MATRIX_SCAN_RATE_CALC
	bool "EC Matrix scan rate calculation [DEPRECATED]"
	depends on ZMK_KSCAN_EC_MATRIX
	select DEPRECATED
	select ZMK_ANALOG_MATRIX_SCAN_RATE_CALC

config ZMK_KSCAN_EC_MATRIX_READ_TIMING
	bool "EC Matrix read timing detail capture"
	depends on ZMK_KSCAN_EC_MATRIX
	select TIMING_FUNCTIONS

config ZMK_KSCAN_EC_MATRIX_SETTINGS_INIT_LOAD
	bool "EC Matrix Settings auto load on start [DEPRECATED]"
	select DEPRECATED
	depends on ZMK_KSCAN_EC_MATRIX_SETTINGS

config ZMK_KSCAN_EC_MATRIX_THREAD_PRIORITY
	int "Thread priority"
	default -1
	help
	  Priority of thread used by the driver to handle interrupts.

config ZMK_KSCAN_EC_MATRIX_THREAD_STACK_SIZE
	int "Thread stack size"
	default 768
	help
	  Stack size of thread used by the driver to handle interrupts.

config ZMK_KSCAN_EC_MATRIX_CALIBRATOR
	bool "Build in the calibrator needed for initial setup of the matrix [DEPRECATED]"
	select DEPRECATED
	select ZMK_ANALOG_MATRIX_CALIBRATOR

if ZMK_KSCAN_EC_MATRIX_CALIBRATOR

config ZMK_KSCAN_EC_MATRIX_VERBOSE_CALIBRATOR
	bool "Verbose Calibration [DEPRECATED]"
	select DEPRECATED
	select ZMK_ANALOG_MATRIX_VERBOSE_CALIBRATOR

endif # ZMK_KSCAN_EC_MATRIX_CALIBRATOR

endif # ZMK_KSCAN_EC_MATRIX
