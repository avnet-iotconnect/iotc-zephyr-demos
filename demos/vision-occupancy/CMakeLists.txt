# vision-occupancy demo: edge person detection on the MIMXRT1170-EVKB.
#
# Needs two modules on top of Zephyr:
#   - iotc-zephyr-sdk (pass -DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk)
#   - tflite-micro (a Zephyr optional module; in this repo's west.yml, or in a
#     vanilla zephyrproject workspace:
#       west config manifest.project-filter -- +tflite-micro
#       west update tflite-micro)
# Build with the camera shield:
#   west build -b mimxrt1170_evk/mimxrt1176/cm7 --shield nxp_btb44_ov5640 ...
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_demo_vision_occupancy)

if(NOT DEFINED ZEPHYR_TFLITE_MICRO_MODULE_DIR)
  message(FATAL_ERROR
    "tflite-micro module not in the workspace. Enable it with:\n"
    "  west config manifest.project-filter -- +tflite-micro\n"
    "  west update tflite-micro")
endif()

target_sources(app PRIVATE
  src/main.c
  src/vision_camera.c
  src/vision_infer.cpp
  src/model_store.c
  src/png_gray.c
)

# quickstart_credentials.h: PUBLIC CA roots only (no device key). The
# per-device key/cert is provisioned on-chip (iotcprov provision) into NVS.
target_include_directories(app PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../quickstart/src)

# Built-in model: embed the stock TFLM person-detect .tflite (int8, 96x96x1,
# Apache-2.0) straight from the tflite-micro module -- no binary in this repo.
set(PERSON_DETECT_TFLITE
  ${ZEPHYR_TFLITE_MICRO_MODULE_DIR}/tensorflow/lite/micro/models/person_detect.tflite)
generate_inc_file_for_target(app
  ${PERSON_DETECT_TFLITE}
  ${ZEPHYR_BINARY_DIR}/include/generated/person_detect_tflite.inc)
target_include_directories(app PRIVATE ${ZEPHYR_BINARY_DIR}/include/generated)
