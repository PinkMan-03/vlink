#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

cmake_minimum_required(VERSION 3.15...3.31)

project(example_security_rsa LANGUAGES CXX)

include(GNUInstallDirs)

if(NOT TARGET vlink::vlink)
  find_package(vlink QUIET)
endif()
if(NOT TARGET vlink::vlink)
  message(FATAL_ERROR "Can not find_package for vlink.")
endif()

if(NOT TARGET OpenSSL::Crypto)
  find_package(OpenSSL 3.0.0 QUIET)
endif()
if(NOT TARGET OpenSSL::Crypto)
  message(WARNING "OpenSSL::Crypto >= 3.0.0 not found -- skipping example_security_rsa.")
  return()
endif()

add_executable(${PROJECT_NAME} security_rsa.cc)

target_link_libraries(${PROJECT_NAME} vlink::all OpenSSL::Crypto)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
