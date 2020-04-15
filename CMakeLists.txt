cmake_minimum_required(VERSION 3.13)
project(Forest)
include(ExternalProject)
enable_testing()

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS -Wall)

ExternalProject_Add(
  gtest
  PREFIX gtest
  URL https://github.com/google/googletest/archive/release-1.10.0.tar.gz
  URL_HASH SHA256=9dc9157a9a1551ec7a7e43daea9a694a0bb5fb8bec81235d8a1e6ef64c716dcb
  INSTALL_COMMAND ""
)

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
add_subdirectory(third_party)
add_subdirectory(test)
