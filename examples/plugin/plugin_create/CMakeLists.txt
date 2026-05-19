#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

cmake_minimum_required(VERSION 3.15...3.31)

project(example_plugin_create LANGUAGES CXX)

include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/example_standalone.cmake")

include(GNUInstallDirs)

if(NOT TARGET vlink::vlink)
  find_package(vlink QUIET)
endif()
if(NOT TARGET vlink::vlink)
  message(FATAL_ERROR "Can not find_package for vlink.")
endif()

# --- Plugin shared library (calculator_plugin.so) ---
add_library(calculator_plugin SHARED calculator_plugin.cc)
target_link_libraries(calculator_plugin vlink::all)
set_target_properties(
  calculator_plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
                               RUNTIME_OUTPUT_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
)
foreach(OUTPUTCONFIG DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
  if(DEFINED CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG})
    set(_plugin_runtime_output_dir ${CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG}})
  else()
    set(_plugin_runtime_output_dir ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
  endif()
  set_target_properties(
    calculator_plugin PROPERTIES LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${_plugin_runtime_output_dir}
                                 RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} ${_plugin_runtime_output_dir}
  )
endforeach()

# --- Host executable (loader) ---
add_executable(${PROJECT_NAME} loader.cc)
target_link_libraries(${PROJECT_NAME} vlink::all)

install(
  TARGETS ${PROJECT_NAME} calculator_plugin
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
