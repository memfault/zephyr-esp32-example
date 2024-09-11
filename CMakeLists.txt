# SPDX-License-Identifier: Apache-2.0

# Default board
set(BOARD esp32s3_devkitm/esp32s3/procpu)

cmake_minimum_required(VERSION 3.20.0)

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
