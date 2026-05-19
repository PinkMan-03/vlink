#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

cmake_minimum_required(VERSION 3.15...3.31)

project(example_hello_rpc LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/example_standalone.cmake")

if(NOT TARGET vlink::vlink)
  find_package(vlink QUIET COMPONENTS ALL)
endif()
if(NOT TARGET vlink::vlink)
  message(FATAL_ERROR "Can not find_package for vlink.")
endif()

# Calculator server
add_executable(example_calculator_server calculator_server.cc)
target_include_directories(example_calculator_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(example_calculator_server PRIVATE vlink::all)

# Calculator client (demonstrates all 5 invocation modes)
add_executable(example_calculator_client calculator_client.cc)
target_include_directories(example_calculator_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(example_calculator_client PRIVATE vlink::all)

install(TARGETS example_calculator_server example_calculator_client RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
