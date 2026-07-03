# Reference telemetry demo.
#
# Reuses the canonical app from iotc-zephyr-sdk/samples/telemetry (no logic
# duplication) and supplies this demo's own prj.conf + boards/ overlays. The
# device credential header (src/device_credentials.h) is also taken from the SDK
# sample's src dir, so `python creds/gen_creds_header.py` provisions both.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_demo_telemetry)

if(NOT DEFINED IOTC_SDK_SAMPLE_DIR)
  set(IOTC_SDK_SAMPLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../iotc-zephyr-sdk/samples/telemetry)
endif()

if(NOT EXISTS ${IOTC_SDK_SAMPLE_DIR}/src/main.c)
  message(FATAL_ERROR
    "iotc-zephyr-sdk sample not found at ${IOTC_SDK_SAMPLE_DIR}. "
    "Set -DIOTC_SDK_SAMPLE_DIR=<path>/iotc-zephyr-sdk/samples/telemetry.")
endif()

target_sources(app PRIVATE ${IOTC_SDK_SAMPLE_DIR}/src/main.c)
target_include_directories(app PRIVATE ${IOTC_SDK_SAMPLE_DIR}/src) # device_credentials.h
