# eIQ predictive-maintenance -- vibration (FRDM-MCXN947 + ML Vibro Sens Click).
# Capture mode (default) is network-free. Connect mode (-DEXTRA_CONF_FILE=
# connect.conf) pulls in the iotc-zephyr-sdk module (via ZEPHYR_EXTRA_MODULES)
# and the baked device credentials header.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_eiq_pdm_vibration)

target_sources(app PRIVATE src/main.c)

# quickstart_credentials.h: PUBLIC CA roots only (no device key). Connect mode
# provisions the per-device key/cert on-chip (iotcprov provision) into NVS.
target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../quickstart/src)

# Optional eIQ Time Series Studio model. Drop the TSS-generated deployment
# package (libtss.a + TimeSeries.h) into ./model/ and it links into the connect
# build, replacing the RMS heuristic with the trained per-sample classifier.
# The library is NXP-proprietary (git-ignored); see model/README.md to generate.
set(EIQ_MODEL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/model)
if(CONFIG_IOTCONNECT AND EXISTS ${EIQ_MODEL_DIR}/libtss.a
                     AND EXISTS ${EIQ_MODEL_DIR}/TimeSeries.h)
  message(STATUS "eiq-pdm-vibration: linking eIQ TSS model from ${EIQ_MODEL_DIR}")
  target_include_directories(app PRIVATE ${EIQ_MODEL_DIR})
  target_link_libraries(app PRIVATE ${EIQ_MODEL_DIR}/libtss.a)
  target_compile_definitions(app PRIVATE HAVE_EIQ_MODEL)
endif()
