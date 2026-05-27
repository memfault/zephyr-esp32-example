# SPDX-License-Identifier: Apache-2.0

# Default board
set(BOARD esp32s3_devkitm/esp32s3/procpu)

cmake_minimum_required(VERSION 3.20.0)

# Select a version-specific config overlay before Zephyr is loaded
file(READ "$ENV{ZEPHYR_BASE}/VERSION" _zephyr_version_file)
string(REGEX MATCH "VERSION_MAJOR = ([0-9]+)" _ ${_zephyr_version_file})
set(_zephyr_major ${CMAKE_MATCH_1})
string(REGEX MATCH "VERSION_MINOR = ([0-9]+)" _ ${_zephyr_version_file})
set(_zephyr_minor ${CMAKE_MATCH_1})
string(REGEX MATCH "PATCHLEVEL = ([0-9]+)" _ ${_zephyr_version_file})
set(_zephyr_patch ${CMAKE_MATCH_1})

if("${_zephyr_major}.${_zephyr_minor}.${_zephyr_patch}" VERSION_GREATER_EQUAL "4.4.99")
  set(OVERLAY_CONFIG "${OVERLAY_CONFIG};${CMAKE_CURRENT_SOURCE_DIR}/overlays/zephyr-v4.5.conf")
elseif("${_zephyr_major}.${_zephyr_minor}.${_zephyr_patch}" VERSION_GREATER_EQUAL "4.3.99")
  set(OVERLAY_CONFIG "${OVERLAY_CONFIG};${CMAKE_CURRENT_SOURCE_DIR}/overlays/zephyr-v4.4.conf")
else()
  set(OVERLAY_CONFIG "${OVERLAY_CONFIG};${CMAKE_CURRENT_SOURCE_DIR}/overlays/zephyr-v4.3.conf")
endif()

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(wifi)

FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
zephyr_include_directories(config)

# Generate a git hash that's used as part of the software_version, eg
# 1.0.0+12345678. Permit overriding at command line for CI builds.
if (NOT DEFINED ZEPHYR_MEMFAULT_EXAMPLE_GIT_SHA1)
  EXECUTE_PROCESS(
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMAND git rev-parse --short HEAD
    RESULT_VARIABLE commit_sha1
    OUTPUT_VARIABLE ZEPHYR_MEMFAULT_EXAMPLE_GIT_SHA1
    OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY
  )
endif()

zephyr_compile_definitions(
  ZEPHYR_MEMFAULT_EXAMPLE_GIT_SHA1=\"${ZEPHYR_MEMFAULT_EXAMPLE_GIT_SHA1}\"
)
