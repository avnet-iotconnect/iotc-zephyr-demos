# eIQ predictive-maintenance -- vibration (FRDM-MCXN947 + ML Vibro Sens Click).
# Capture mode (default) is network-free. Connect mode (-DEXTRA_CONF_FILE=
# connect.conf) pulls in the iotc-zephyr-sdk module (via ZEPHYR_EXTRA_MODULES)
# and the baked device credentials header.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_eiq_pdm_vibration)

target_sources(app PRIVATE src/main.c)

# device_credentials.h (device cert/key + CA roots) lives with the SDK sample;
# only #included in connect mode. Harmless on the capture build.
if(NOT DEFINED IOTC_SDK_SAMPLE_DIR)
  set(IOTC_SDK_SAMPLE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../iotc-zephyr-sdk/samples/telemetry)
endif()
target_include_directories(app PRIVATE ${IOTC_SDK_SAMPLE_DIR}/src)
