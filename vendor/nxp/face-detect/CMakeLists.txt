# face-detect: on-device face detection (OV7670 camera + LCD-PAR-S035) with
# detections published to Avnet /IOTCONNECT over cellular (LTE IoT 12 Click,
# generic Zephyr cellular modem driver + PPP).
#
# Vision/inference sources come from the original iotc-mcx-zephyr-demos app
# (EBV Elektronik / NXP); the cloud layer is the iotc-zephyr-sdk with the
# quickstart provisioning flow (src/cloud.c replaces the vendored iotc glue).
#
# Requires the tflite-micro module (in this repo's west.yml) and the
# iotc-zephyr-sdk module. The NPU-accelerated model needs NXP's eIQ Neutron
# TFLM middleware, which upstream Zephyr does not ship -- this port runs the
# software model with CMSIS-NN kernels.
cmake_minimum_required(VERSION 3.20.0)

set(SHIELD lcd_par_s035_8080 dvp_20pin_ov7670)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_demo_face_detect)

if(NOT DEFINED ZEPHYR_TFLITE_MICRO_MODULE_DIR)
  message(FATAL_ERROR
    "tflite-micro module not in the workspace. Enable it with:\n"
    "  west config manifest.project-filter -- +tflite-micro\n"
    "  west update tflite-micro")
endif()

if(CONFIG_MCUX_HAS_NPU)
  message(FATAL_ERROR
    "The NPU model path needs NXP's eIQ Neutron TFLM middleware (prebuilt "
    "libtflm + libNeutron*), which upstream Zephyr does not ship. Build "
    "without CONFIG_MCUX_HAS_NPU to use the software model.")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../common/cmake/iotc_sdk_samples.cmake)

zephyr_include_directories(
  src
  src/img
  src/model
  src/model/model_sw
  src/font
)

target_sources(app PRIVATE
  src/main.c
  src/cloud.c
  src/display.c
  src/convert.c
  src/face_det.cpp
  src/img/image_conv.c
  src/img/image_draw.c
  src/img/image_resize_nearest.c
  src/model/model.cpp
  src/model/get_top_n.cpp
  src/model/yolo_post_processing.cpp
  src/model/model_sw/model_mobilenet_ops_micro.cpp
)

# quickstart_credentials.h: PUBLIC CA roots only (no device key). The
# per-device key/cert is provisioned on-chip (iotcprov provision) into NVS.
target_include_directories(app PRIVATE ${IOTC_SDK_SAMPLES_DIR}/quickstart/src)

# NOTE: do NOT define TF_LITE_STATIC_MEMORY here. The original demo needed it
# to match NXP's prebuilt libtflm.a; the upstream tflite-micro module builds
# WITHOUT it, and defining it only for app sources changes TfLiteTensor's
# layout and corrupts every tensor accessor (garbage dims/type).
target_compile_options(app PRIVATE -Wno-psabi)
