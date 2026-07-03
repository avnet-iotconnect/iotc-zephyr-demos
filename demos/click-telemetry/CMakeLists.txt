# click-telemetry demo: auto-detect MikroE Click sensors -> IOTCONNECT.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_demo_click_telemetry)

if(NOT DEFINED IOTC_SDK_SAMPLE_DIR)
  set(IOTC_SDK_SAMPLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../iotc-zephyr-sdk/samples/telemetry)
endif()

target_sources(app PRIVATE src/main.c)
target_include_directories(app PRIVATE
  ${IOTC_SDK_SAMPLE_DIR}/src                       # device_credentials.h (non-TF-M)
  ${CMAKE_CURRENT_SOURCE_DIR}/../quickstart/src)   # quickstart_credentials.h (TF-M /ns: public CAs only)
