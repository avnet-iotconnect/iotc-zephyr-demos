# eIQ predictive-maintenance -- vibration capture (Phase 1).
# Network-free: reads the FXLS8974 accelerometer via the Zephyr sensor API and
# prints CSV for eIQ Time Series Studio. No iotc-c-lib / SDK dependency yet.
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(iotc_eiq_pdm_vibration_capture)

target_sources(app PRIVATE src/main.c)
