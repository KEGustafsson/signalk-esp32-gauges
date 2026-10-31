# Host unit tests for the shared library's logic that does not need a board:
# value conversion and scales (gauge_resolve, units), the factory pages, the
# page card's rules and schema (page_rules), what a page accepts from Signal
# K and when it stops trusting it (value_gate, sk_parse). CI runs them with
# AddressSanitizer and UndefinedBehaviorSanitizer. See tests/README.md.
cmake_minimum_required(VERSION 3.16)
project(gauge_tests CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
# The same ArduinoJson as the firmware (SensESP's dependency), header-only.
FetchContent_Declare(arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.3
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(arduinojson)

set(COMMON ${CMAKE_CURRENT_SOURCE_DIR}/../common/src)

add_executable(gauge_tests
  test_main.cpp
  test_resolve.cpp
  test_factory.cpp
  test_page_rules.cpp
  test_value_gate.cpp
  test_sk_parse.cpp
  ${COMMON}/gauge_defs.cpp
  ${COMMON}/gauge_resolve.cpp
  ${COMMON}/units.cpp
  ${COMMON}/page_rules.cpp
  ${COMMON}/value_gate.cpp)
target_include_directories(gauge_tests PRIVATE ${COMMON}
                           ${arduinojson_SOURCE_DIR}/src)
target_compile_options(gauge_tests PRIVATE -Wall -Wextra -Werror
                       -fsanitize=address,undefined -fno-sanitize-recover=all)
target_link_options(gauge_tests PRIVATE -fsanitize=address,undefined)
