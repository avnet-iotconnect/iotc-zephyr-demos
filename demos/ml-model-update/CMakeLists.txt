# ml-model-update demo: cloud-updatable ML (IOTM blob) on IO1 Xplained Pro
# sensors. Own src/main.c; uses the quickstart provisioning flow (public CA
# roots only). Regenerate src/model_builtin.h with tools/build_model.py if
# the blob format changes.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_demo_ml_model_update)

target_sources(app PRIVATE src/main.c)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../common/cmake/iotc_sdk_samples.cmake)

# quickstart_credentials.h: PUBLIC CA roots only (no device key). The
# per-device key/cert is provisioned on-chip (iotcprov provision) into NVS.
target_include_directories(app PRIVATE ${IOTC_SDK_SAMPLES_DIR}/quickstart/src)
