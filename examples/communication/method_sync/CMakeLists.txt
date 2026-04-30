#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

cmake_minimum_required(VERSION 3.15...3.31)

project(example_method_sync LANGUAGES CXX)

include(GNUInstallDirs)

if(NOT TARGET vlink::vlink)
  find_package(vlink QUIET COMPONENTS all)
endif()
if(NOT TARGET vlink::vlink)
  message(FATAL_ERROR "Can not find_package for vlink.")
endif()

# Single-process combined example (original)
add_executable(${PROJECT_NAME} method_sync.cc)
target_link_libraries(${PROJECT_NAME} vlink::all)

# Multi-process split: server
add_executable(${PROJECT_NAME}_server server.cc)
target_link_libraries(${PROJECT_NAME}_server vlink::all)

# Multi-process split: client
add_executable(${PROJECT_NAME}_client client.cc)
target_link_libraries(${PROJECT_NAME}_client vlink::all)

install(TARGETS ${PROJECT_NAME} ${PROJECT_NAME}_server ${PROJECT_NAME}_client
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
