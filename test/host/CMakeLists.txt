# Host-side unit tests (no ESP-IDF). Build on any x86 machine / CI:
#
#   cmake -S test/host -B build-host && cmake --build build-host && ctest --test-dir build-host
#
# Compiles the real schedule_engine.cpp against the host toolchain so the pure
# scheduling logic is verified without flashing hardware.
cmake_minimum_required(VERSION 3.16)
project(aquarium_host_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT MSVC)
  add_compile_options(-Wall -Wextra)
endif()

set(COMPONENT ${CMAKE_CURRENT_SOURCE_DIR}/../../components/aquarium_app)

add_executable(test_schedule_engine
  test_schedule_engine.cpp
  ${COMPONENT}/src/schedule_engine.cpp
)
target_include_directories(test_schedule_engine PRIVATE ${COMPONENT}/include)

enable_testing()
add_test(NAME schedule_engine COMMAND test_schedule_engine)
