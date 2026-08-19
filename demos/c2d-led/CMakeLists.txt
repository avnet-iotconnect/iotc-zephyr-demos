# c2d-led demo: cloud-to-device LED control.
# The app source lives in iotc-zephyr-sdk/samples/c2d-led; this demo supplies
# its own prj.conf + boards/ and reuses the SDK telemetry sample's generated
# device_credentials.h.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_demo_c2d_led)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../common/cmake/iotc_sdk_samples.cmake)

target_sources(app PRIVATE ${IOTC_SDK_SAMPLES_DIR}/c2d-led/src/main.c)
target_include_directories(app PRIVATE ${IOTC_SDK_SAMPLES_DIR}/telemetry/src) # device_credentials.h
