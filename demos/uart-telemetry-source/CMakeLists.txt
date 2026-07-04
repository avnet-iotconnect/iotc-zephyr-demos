# IOTCONNECT UART telemetry-source demo.
#
# For connectivity-less MCUs (no Ethernet/Wi-Fi/IP transport in their Zephyr
# port): compiles the portable iotc-c-lib protocol core + cJSON directly into the
# app and emits IOTCONNECT 2.1 telemetry JSON over the console UART every few
# seconds. A gateway relays that JSON to the cloud (over CAN FD / UART, or a
# future cell-modem bearer). Unlike the other demos, this one does NOT pull in
# the iotc-zephyr-sdk module (no net/TLS/MQTT stack) -- it needs only iotc-c-lib.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_uart_telemetry_source)

# Locate iotc-c-lib: -DZEPHYR_IOTC_C_LIB_MODULE_DIR=..., then the environment
# variable of the same name, then the conventional west workspace path.
set(IOTC_C_LIB_DIR ${ZEPHYR_IOTC_C_LIB_MODULE_DIR})
if(NOT IOTC_C_LIB_DIR)
  set(IOTC_C_LIB_DIR $ENV{ZEPHYR_IOTC_C_LIB_MODULE_DIR})
endif()
if(NOT IOTC_C_LIB_DIR)
  set(IOTC_C_LIB_DIR ${ZEPHYR_BASE}/../modules/lib/iotc-c-lib)
endif()
if(NOT EXISTS ${IOTC_C_LIB_DIR}/core/src/iotcl.c)
  message(FATAL_ERROR
    "iotc-c-lib not found at '${IOTC_C_LIB_DIR}'.\n"
    "Pass -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<path-to-iotc-c-lib> "
    "or set the environment variable of the same name.")
endif()

target_sources(app PRIVATE
  src/main.c
  ${IOTC_C_LIB_DIR}/core/src/iotcl.c
  ${IOTC_C_LIB_DIR}/core/src/iotcl_c2d.c
  ${IOTC_C_LIB_DIR}/core/src/iotcl_internal.c
  ${IOTC_C_LIB_DIR}/core/src/iotcl_telemetry.c
  ${IOTC_C_LIB_DIR}/core/src/iotcl_util.c
  ${IOTC_C_LIB_DIR}/lib/cJSON/cJSON.c
)

target_include_directories(app PRIVATE
  ${IOTC_C_LIB_DIR}/core/include
  ${IOTC_C_LIB_DIR}/lib/cJSON
)
