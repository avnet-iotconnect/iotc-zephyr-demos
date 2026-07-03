# SPDX-License-Identifier: MIT
#
# IOTCONNECT NXP eIQ Neutron NPU benchmark -- vendor-specific demo.
#
# Proves NPU acceleration by running the SAME quantized model twice -- once on
# the eIQ Neutron NPU, once on the Cortex-M33 -- timing each inference and
# streaming the result to /IOTCONNECT. The NPU-vs-CPU build is a single Kconfig
# switch, CONFIG_MCUX_HAS_NPU (CPU build via cpu.conf).
#
# This app single-sources the eIQ/Neutron model glue from iotc-mcx-zephyr-demos
# (it does not copy it) and uses the IOTCONNECT Zephyr SDK for connectivity.
#
# Required modules on the `west build` line (see README):
#   -DZEPHYR_EXTRA_MODULES=<abs path to iotc-zephyr-sdk>
#   -DZEPHYR_IOTC_C_LIB_MODULE_DIR=<abs path to iotc-c-lib>
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_npu_benchmark)

# --- Reused eIQ/Neutron model glue (override with -DIOTC_MCX_DEMOS_DIR=...) ---
if(NOT DEFINED IOTC_MCX_DEMOS_DIR)
  set(IOTC_MCX_DEMOS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../iotc-mcx-zephyr-demos)
endif()
set(MODEL_DIR ${IOTC_MCX_DEMOS_DIR}/src/model)

# --- eIQ-augmented TensorFlow Lite Micro (NXP Neutron build) -----------------
# This is the "eIQ package": NXP's mcux-sdk-middleware-tensorflow repo
# (github.com/nxp-mcuxpresso, tag MCUX_2.16.000 -- pinned in
# iotc-mcx-zephyr-demos/west.yml). It carries the Neutron kernel + prebuilt
# libtflm.a / libNeutron*.a that upstream tflite-micro lacks. west-update or
# clone it to <workspace>/modules/lib/tflite-micro. See README -> "Drop in".
if(NOT DEFINED TFLITE_DIR)
  set(TFLITE_DIR ${IOTC_MCX_DEMOS_DIR}/../modules/lib/tflite-micro)
endif()

if(NOT EXISTS ${MODEL_DIR}/model.cpp)
  message(FATAL_ERROR
    "Model glue not found at ${MODEL_DIR}. Set -DIOTC_MCX_DEMOS_DIR to your "
    "iotc-mcx-zephyr-demos checkout (see README -> 'eIQ artifact prerequisites').")
endif()

# --- App sources -------------------------------------------------------------
#   main.c          : net bring-up + SDK lifecycle + benchmark loop (pure C)
#   model_bench.cpp : tiny C-callable shim over the C++ MODEL_* API
#   model.cpp       : the reused TFLM interpreter wrapper (NXP)
target_sources(app PRIVATE
  src/main.c
  src/model_bench.cpp
  ${MODEL_DIR}/model.cpp
)

zephyr_include_directories(
  src
  ${MODEL_DIR}
  ${TFLITE_DIR}
  ${TFLITE_DIR}/third_party/flatbuffers/include
  ${TFLITE_DIR}/third_party/gemmlowp
  ${TFLITE_DIR}/third_party/ruy
)

# --- Prebuilt TFLM static library (per Cortex-M core) ------------------------
add_library(libtflm STATIC IMPORTED GLOBAL)
if(DEFINED CONFIG_CPU_CORTEX_M33)
  set_target_properties(libtflm PROPERTIES IMPORTED_LOCATION
    ${TFLITE_DIR}/lib/cm33/armgcc/libtflm.a)
elseif(DEFINED CONFIG_CPU_CORTEX_M7)
  set_target_properties(libtflm PROPERTIES IMPORTED_LOCATION
    ${TFLITE_DIR}/lib/cm7/armgcc/libtflm.a)
else()
  message(FATAL_ERROR "No prebuilt libtflm.a for this core")
endif()

if(DEFINED CONFIG_MCUX_HAS_NPU)
  # ---- NPU path: Neutron-compiled graph + Neutron runtime -------------------
  zephyr_include_directories(
    ${MODEL_DIR}/model_npu
    ${TFLITE_DIR}/tensorflow/lite/micro/kernels/neutron
    ${TFLITE_DIR}/third_party/neutron/common/include
    ${TFLITE_DIR}/third_party/neutron/driver/include
  )
  target_sources(app PRIVATE
    ${MODEL_DIR}/model_npu/model_ops_npu.cpp
    ${MODEL_DIR}/model_npu/model_data.s          # eIQ Neutron-converter output
    ${TFLITE_DIR}/tensorflow/lite/micro/kernels/neutron/neutron.cpp
  )
  add_library(libNeutronDriver STATIC IMPORTED GLOBAL)
  set_target_properties(libNeutronDriver PROPERTIES IMPORTED_LOCATION
    ${TFLITE_DIR}/third_party/neutron/mcxn/libNeutronDriver.a)
  add_library(libNeutronFirmware STATIC IMPORTED GLOBAL)
  set_target_properties(libNeutronFirmware PROPERTIES IMPORTED_LOCATION
    ${TFLITE_DIR}/third_party/neutron/mcxn/libNeutronFirmware.a)
  target_link_libraries(app PUBLIC libNeutronDriver libNeutronFirmware)
else()
  # ---- CPU path: same model topology, reference/CMSIS-NN kernels -------------
  zephyr_include_directories(${MODEL_DIR}/model_sw)
  target_sources(app PRIVATE ${MODEL_DIR}/model_sw/model_mobilenet_ops_micro.cpp)
endif()

target_link_libraries(app PUBLIC libtflm)
target_compile_definitions(app PRIVATE TF_LITE_STATIC_MEMORY)
target_compile_options(app PRIVATE -Wno-psabi)
