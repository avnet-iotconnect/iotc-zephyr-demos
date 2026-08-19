# Locates the IOTCONNECT Zephyr SDK's samples/ tree, whose apps the demos in
# this repo reuse (no logic duplication). Include AFTER find_package(Zephyr)
# so the Zephyr module variables are populated. Sets IOTC_SDK_SAMPLES_DIR.
#
# Resolution order:
#   1. -DIOTC_SDK_SAMPLES_DIR=<path>/iotc-zephyr-sdk/samples  (explicit)
#   2. the `iotconnect` Zephyr module (west workspace or ZEPHYR_EXTRA_MODULES)
#   3. a sibling iotc-zephyr-sdk checkout next to this repo

if(DEFINED IOTC_SDK_SAMPLE_DIR AND NOT DEFINED IOTC_SDK_SAMPLES_DIR)
  # Back-compat: the old flag pointed at samples/telemetry specifically.
  get_filename_component(IOTC_SDK_SAMPLES_DIR ${IOTC_SDK_SAMPLE_DIR} DIRECTORY)
endif()

if(NOT DEFINED IOTC_SDK_SAMPLES_DIR)
  if(DEFINED ZEPHYR_IOTCONNECT_MODULE_DIR)
    set(IOTC_SDK_SAMPLES_DIR ${ZEPHYR_IOTCONNECT_MODULE_DIR}/samples)
  else()
    get_filename_component(IOTC_SDK_SAMPLES_DIR
      ${CMAKE_CURRENT_LIST_DIR}/../../../iotc-zephyr-sdk/samples ABSOLUTE)
  endif()
endif()

if(NOT EXISTS ${IOTC_SDK_SAMPLES_DIR}/telemetry/src/main.c)
  message(FATAL_ERROR
    "iotc-zephyr-sdk samples not found at ${IOTC_SDK_SAMPLES_DIR}. "
    "Pass -DIOTC_SDK_SAMPLES_DIR=<path>/iotc-zephyr-sdk/samples.")
endif()
