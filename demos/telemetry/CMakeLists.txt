# Reference telemetry demo.
#
# Reuses the canonical app from iotc-zephyr-sdk/samples/telemetry (no logic
# duplication) and supplies this demo's own prj.conf + boards/ overlays. The
# device credential header (src/device_credentials.h) is also taken from the SDK
# sample's src dir, so `python creds/gen_creds_header.py` provisions both.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_demo_telemetry)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../common/cmake/iotc_sdk_samples.cmake)

target_sources(app PRIVATE ${IOTC_SDK_SAMPLES_DIR}/telemetry/src/main.c)
target_include_directories(app PRIVATE ${IOTC_SDK_SAMPLES_DIR}/telemetry/src) # device_credentials.h
