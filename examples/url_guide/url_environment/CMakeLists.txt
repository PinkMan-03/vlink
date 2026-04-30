#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

cmake_minimum_required(VERSION 3.15...3.31)

project(example_url_environment LANGUAGES CXX)

include(GNUInstallDirs)

if(NOT TARGET vlink::vlink)
  find_package(vlink QUIET COMPONENTS all)
endif()
if(NOT TARGET vlink::vlink)
  message(FATAL_ERROR "Can not find_package for vlink.")
endif()

add_executable(${PROJECT_NAME} url_environment.cc)

target_link_libraries(${PROJECT_NAME} vlink::all)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
