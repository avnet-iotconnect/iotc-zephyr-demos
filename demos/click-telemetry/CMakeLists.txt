# click-telemetry demo: auto-detect MikroE Click sensors -> IOTCONNECT.
# The app source lives in iotc-zephyr-sdk/samples/click-telemetry; this demo
# supplies its own prj.conf + boards/ overlays.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_demo_click_telemetry)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../common/cmake/iotc_sdk_samples.cmake)

target_sources(app PRIVATE ${IOTC_SDK_SAMPLES_DIR}/click-telemetry/src/main.c)
target_include_directories(app PRIVATE
  ${IOTC_SDK_SAMPLES_DIR}/telemetry/src      # device_credentials.h (non-TF-M)
  ${IOTC_SDK_SAMPLES_DIR}/quickstart/src)    # quickstart_credentials.h (TF-M /ns: public CAs only)
