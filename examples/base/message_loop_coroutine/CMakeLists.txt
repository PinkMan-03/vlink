#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

cmake_minimum_required(VERSION 3.15...3.31)

project(example_message_loop_coroutine LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/example_standalone.cmake")

if(NOT TARGET vlink::vlink)
  find_package(vlink QUIET COMPONENTS ALL)
endif()
if(NOT TARGET vlink::vlink)
  message(FATAL_ERROR "Can not find_package for vlink.")
endif()

add_executable(${PROJECT_NAME} message_loop_coroutine.cc)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU"
   AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 10
   AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11
)
  target_compile_options(${PROJECT_NAME} PRIVATE -fcoroutines)
endif()
target_link_libraries(${PROJECT_NAME} PRIVATE vlink::all)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
