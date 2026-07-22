cmake_minimum_required(VERSION 3.28)
project(M2Suite LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (MSVC)
  add_compile_options(/W4 /permissive-)
endif()

option(M2SUITE_BUILD_TESTS "Build M2Suite unit tests" ON)
option(M2SUITE_BUILD_APPS "Build M2Suite Qt applications" ON)

enable_testing()

# Plain C++17 libraries — no Qt dependency, independently testable/reusable.
add_subdirectory(libs/libm2core)
add_subdirectory(libs/libm2texture)
add_subdirectory(libs/libm2cel)
add_subdirectory(libs/libm2audio)
add_subdirectory(libs/libm2stream)
add_subdirectory(libs/libm2disasm)
add_subdirectory(libs/libm2disc)
add_subdirectory(libs/libm2model)

if (M2SUITE_BUILD_APPS)
  add_subdirectory(apps/m2suite-shell)
endif()

if (M2SUITE_BUILD_TESTS)
  add_subdirectory(tests)
endif()
