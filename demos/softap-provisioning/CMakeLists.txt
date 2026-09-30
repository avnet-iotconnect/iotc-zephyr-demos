# IOTCONNECT Soft-AP provisioning demo -- phone-browser onboarding.
# The app source lives in iotc-zephyr-sdk/samples/softap-provisioning; this
# demo supplies the per-board confs (Wi-Fi AP capable boards only).
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_softap_provisioning)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../common/cmake/iotc_sdk_samples.cmake)

target_sources(app PRIVATE
  ${IOTC_SDK_SAMPLES_DIR}/softap-provisioning/src/main.c
  ${IOTC_SDK_SAMPLES_DIR}/softap-provisioning/src/portal.c
)
target_include_directories(app PRIVATE
  ${IOTC_SDK_SAMPLES_DIR}/softap-provisioning/src
)

# The HTTP server keeps each service's resource table in its own iterable
# linker section; the application registers it (both linker flows).
zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section(
  NAME http_resource_desc_portal_svc
  KVMA RAM_REGION GROUP RODATA_REGION
)
