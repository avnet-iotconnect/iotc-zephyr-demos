# IOTCONNECT Zephyr quickstart -- flash-and-provision binary.
# The app source lives in iotc-zephyr-sdk/samples/quickstart; this demo
# supplies the per-board confs/overlays for the full board matrix.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_quickstart)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../common/cmake/iotc_sdk_samples.cmake)

target_sources(app PRIVATE ${IOTC_SDK_SAMPLES_DIR}/quickstart/src/main.c)
