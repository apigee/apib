cmake_minimum_required(VERSION 3.13)
project(Forest)
include(FetchContent)
enable_testing()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS -Wall)

FetchContent_Declare(
  gtest
  URL https://github.com/google/googletest/archive/release-1.10.0.tar.gz
  URL_HASH SHA256=9dc9157a9a1551ec7a7e43daea9a694a0bb5fb8bec81235d8a1e6ef64c716dcb
)
FetchContent_GetProperties(gtest)
if (NOT gtest_POPULATED)
  FetchContent_Populate(gtest)
  add_subdirectory(${gtest_SOURCE_DIR} ${gtest_BINARY_DIR})
endif()

FetchContent_Declare(
  absl
  URL https://github.com/abseil/abseil-cpp/archive/20200225.1.tar.gz
  URL_HASH SHA256=0db0d26f43ba6806a8a3338da3e646bb581f0ca5359b3a201d8fb8e4752fd5f8
)
FetchContent_GetProperties(absl)
if (NOT absl_POPULATED)
  FetchContent_Populate(absl)
  add_subdirectory(${absl_SOURCE_DIR} ${absl_BINARY_DIR})
endif()

include_directories(
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}/gtest/src/gtest/googletest/include
)
link_directories(
  ${CMAKE_BINARY_DIR}/gtest/src/gtest-build/lib
)

if (DEFINED ENV{OPENSSL_INCLUDE_DIR})
  include_directories($ENV{OPENSSL_INCLUDE_DIR})
endif()
if (DEFINED ENV{OPENSSL_LIB_DIR})
  link_directories($ENV{OPENSSL_LIB_DIR})
endif()

if (DEFINED ENV{EXTRA_INCLUDE_DIR})
  include_directories($ENV{EXTRA_INCLUDE_DIR})
endif()
if (DEFINED ENV{EXTRA_LIB_DIR})
  link_directories($ENV{EXTRA_LIB_DIR})
endif()

add_subdirectory(apib)
add_subdirectory(third_party/base64)
add_subdirectory(third_party/http_parser)
add_subdirectory(test)
