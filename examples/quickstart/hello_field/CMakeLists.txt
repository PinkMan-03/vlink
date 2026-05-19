#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

cmake_minimum_required(VERSION 3.15...3.31)

project(example_hello_field LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/example_standalone.cmake")

if(NOT TARGET vlink::vlink)
  find_package(vlink QUIET COMPONENTS ALL)
endif()
if(NOT TARGET vlink::vlink)
  message(FATAL_ERROR "Can not find_package for vlink.")
endif()

# Setter program -- writes configuration values
add_executable(example_config_setter config_setter.cc)
target_include_directories(example_config_setter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(example_config_setter PRIVATE vlink::all)

# Getter program -- reads configuration values
add_executable(example_config_getter config_getter.cc)
target_include_directories(example_config_getter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(example_config_getter PRIVATE vlink::all)

install(TARGETS example_config_setter example_config_getter RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
