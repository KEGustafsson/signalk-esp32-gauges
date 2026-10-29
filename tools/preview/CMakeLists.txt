# Host preview: renders every gauge page to images with the firmware's own UI
# code (common/src/ui) and LVGL, without any hardware. See README.md.
cmake_minimum_required(VERSION 3.16)
project(gauge_preview C CXX)
set(CMAKE_CXX_STANDARD 17)

include(FetchContent)
FetchContent_Declare(lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v9.3.0
  GIT_SHALLOW TRUE)
FetchContent_GetProperties(lvgl)
if(NOT lvgl_POPULATED)
  FetchContent_Populate(lvgl)
endif()

set(REPO ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(COMMON ${REPO}/common/src)

# Same LVGL configuration as the firmware.
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE)
include_directories(${REPO}/waveshare-esp32-s3-touch-lcd-2.1/include
                    ${lvgl_SOURCE_DIR} ${COMMON})

file(GLOB_RECURSE LVGL_SRC ${lvgl_SOURCE_DIR}/src/*.c)
add_library(lvgl STATIC ${LVGL_SRC})

add_executable(preview preview.cpp
  ${COMMON}/gauge_defs.cpp
  ${COMMON}/ui/draw_utils.cpp
  ${COMMON}/ui/lcd_readout.cpp
  ${COMMON}/ui/gauge_page.cpp
  ${COMMON}/ui/radial_gauge.cpp
  ${COMMON}/ui/compass_gauge.cpp
  ${COMMON}/ui/attitude_gauge.cpp)
target_link_libraries(preview lvgl m)
