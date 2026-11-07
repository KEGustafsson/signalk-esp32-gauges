# SPDX-License-Identifier: EUPL-1.2
# Host unit tests for P4 Gauge Crew Radio's core (src/core): the wire format,
# the channel crypto, the receive pipeline and the mixer, checked against the
# crew-radio app's own vectors (vectors/, copied from crew-radio's
# sk-plugin/test/). CI runs them with AddressSanitizer and UBSan. See
# ../README.md.
cmake_minimum_required(VERSION 3.16)
project(crew_radio_tests C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
# mbedTLS 3.6 LTS, the line ESP-IDF 5.5 (Arduino-ESP32 3.3) ships: the same
# API the node calls on the board. Release tarball (it carries the generated
# files), pinned by its SHA-256.
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(mbedtls
  URL https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.4/mbedtls-3.6.4.tar.bz2
  URL_HASH SHA256=ec35b18a6c593cf98c3e30db8b98ff93e8940a8c4e690e66b41dfc011d678110
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# The same ArduinoJson as the gauge tests, to read the vector files.
FetchContent_Declare(arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.3
  GIT_SHALLOW TRUE
  SOURCE_SUBDIR no-cmake)
FetchContent_MakeAvailable(mbedtls arduinojson)

# libopus as pschatzmann/codec-opus packages it (1.6.1, fixed point): the
# same sources the firmware builds (the 4"'s platformio.ini pins the same
# commit). Compiled without the sanitizers. Off for a run that must not
# build third-party code (the rest of the tests need none of it).
option(CREW_RADIO_OPUS "Build libopus and the Opus tests" ON)
if(CREW_RADIO_OPUS)
  FetchContent_Declare(codec_opus
    GIT_REPOSITORY https://github.com/pschatzmann/codec-opus.git
    GIT_TAG c02de6316dddbb9dffdf976c159c4eaacecdf12f
    SOURCE_SUBDIR no-cmake)
  FetchContent_MakeAvailable(codec_opus)
  file(GLOB_RECURSE OPUS_SRC ${codec_opus_SOURCE_DIR}/src/*.c)
  add_library(opus STATIC ${OPUS_SRC})
  target_include_directories(opus PUBLIC ${codec_opus_SOURCE_DIR}/src)
  target_compile_definitions(opus PRIVATE HAVE_CONFIG_H)
  target_compile_options(opus PRIVATE -O2 -w)
endif()

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
set(GAUGE_TESTS ${CMAKE_CURRENT_SOURCE_DIR}/../../tests)

add_executable(crew_radio_tests
  ${GAUGE_TESTS}/test_main.cpp
  random_host.cpp
  test_vectors.cpp
  test_packet.cpp
  test_hello.cpp
  test_crypto.cpp
  test_rate_limiter.cpp
  test_seen_cache.cpp
  test_seq_tracker.cpp
  test_ingress.cpp
  test_mixer.cpp
  test_lan_peers.cpp
  test_roster.cpp
  test_clock_policy.cpp
  test_supervisor.cpp
  test_self_test.cpp
  test_spsc_ring.cpp
  test_net_node.cpp
  test_playout.cpp
  test_radio_settings.cpp
  test_talk.cpp
  test_radio_state.cpp
  test_lifecycle.cpp
  test_curve25519.cpp
  test_rns_primitives.cpp
  test_rns_vectors.cpp
  test_rns_node.cpp
  ${SRC}/core/packet.cpp
  ${SRC}/core/hello.cpp
  ${SRC}/core/channel_crypto.cpp
  ${SRC}/core/rate_limiter.cpp
  ${SRC}/core/seen_cache.cpp
  ${SRC}/core/seq_tracker.cpp
  ${SRC}/core/ingress.cpp
  ${SRC}/core/conceal.cpp
  ${SRC}/core/mixer.cpp
  ${SRC}/core/lan_peers.cpp
  ${SRC}/core/roster.cpp
  ${SRC}/core/clock_policy.cpp
  ${SRC}/core/self_test.cpp
  ${SRC}/core/playout.cpp
  ${SRC}/core/net_node.cpp
  ${SRC}/core/radio_settings.cpp
  ${SRC}/core/talk_key.cpp
  ${SRC}/core/transmitter.cpp
  ${SRC}/core/tones.cpp
  ${SRC}/core/mic.cpp
  ${SRC}/core/rns/curve25519.cpp
  ${SRC}/core/rns/rns_crypto.cpp
  ${SRC}/core/rns/rns_packet.cpp
  ${SRC}/core/rns/rns_identity.cpp
  ${SRC}/core/rns/rns_link.cpp
  ${SRC}/core/rns/carry.cpp
  ${SRC}/core/rns/reticulum_node.cpp)
target_include_directories(crew_radio_tests PRIVATE ${SRC} ${GAUGE_TESTS}
                           ${arduinojson_SOURCE_DIR}/src)
target_compile_definitions(crew_radio_tests PRIVATE
                           VECTOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors")
target_link_libraries(crew_radio_tests PRIVATE mbedcrypto pthread)
if(CREW_RADIO_OPUS)
  target_sources(crew_radio_tests PRIVATE test_opus.cpp
                 ${SRC}/core/opus_decoder.cpp ${SRC}/core/opus_encoder.cpp)
  target_link_libraries(crew_radio_tests PRIVATE opus)
endif()
target_compile_options(crew_radio_tests PRIVATE -Wall -Wextra -Werror)
option(SANITIZE "Build with AddressSanitizer and UBSan" ON)
if(SANITIZE)
  target_compile_options(crew_radio_tests PRIVATE
                         -fsanitize=address,undefined -fno-sanitize-recover=all)
  target_link_options(crew_radio_tests PRIVATE -fsanitize=address,undefined)
endif()
