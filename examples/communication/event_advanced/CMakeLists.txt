#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

cmake_minimum_required(VERSION 3.15...3.31)

project(example_event_advanced LANGUAGES CXX)

include(GNUInstallDirs)

if(NOT TARGET vlink::vlink)
  find_package(vlink QUIET COMPONENTS all)
endif()
if(NOT TARGET vlink::vlink)
  message(FATAL_ERROR "Can not find_package for vlink.")
endif()

# Single-process combined example (original)
add_executable(${PROJECT_NAME} event_advanced.cc)
target_link_libraries(${PROJECT_NAME} vlink::all)

# Multi-process split: publisher
add_executable(${PROJECT_NAME}_publisher publisher_main.cc)
target_link_libraries(${PROJECT_NAME}_publisher vlink::all)

# Multi-process split: subscriber
add_executable(${PROJECT_NAME}_subscriber subscriber_main.cc)
target_link_libraries(${PROJECT_NAME}_subscriber vlink::all)

install(TARGETS ${PROJECT_NAME} ${PROJECT_NAME}_publisher ${PROJECT_NAME}_subscriber
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
