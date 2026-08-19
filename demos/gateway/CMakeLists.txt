# gateway demo: IOTCONNECT gateway + eMMC store-and-forward on FRDM-i.MX93.
# Needs the iotc-zephyr-sdk module (-DZEPHYR_EXTRA_MODULES=<path>/iotc-zephyr-sdk).
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_demo_gateway)

target_sources(app PRIVATE
  src/main.c
  src/uart_ingest.c
  src/spool.c
)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../common/cmake/iotc_sdk_samples.cmake)

# quickstart_credentials.h: PUBLIC CA roots only (no device key). The
# per-device key/cert is provisioned on-chip (iotcprov provision).
target_include_directories(app PRIVATE
  ${IOTC_SDK_SAMPLES_DIR}/quickstart/src)
