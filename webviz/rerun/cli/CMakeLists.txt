#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

project(vlink-bag2rrd LANGUAGES CXX)

add_executable(${PROJECT_NAME} bag2rrd.cc)

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/../src ${PROJECT_SOURCE_DIR}/../..)

target_sources(
  ${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/../src/rerun_converter.cc
                          ${PROJECT_SOURCE_DIR}/../src/rerun_converter.h
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE vlink::vlink
          vlink::proxy_api
          ${protobuf_libs}
          ${flatbuffers_libs}
          thirdparty::json
          thirdparty::argparse
          thirdparty::exprtk
          rerun_sdk
)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# symlinks
if(ENABLE_SYMLINKS)
  vlink_install_symlink(bag2rrd SYMLINK ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# completions
if(ENABLE_COMPLETIONS)
  if(ENABLE_SYMLINKS)
    vlink_install_completions(${PROJECT_NAME} ALIAS bag2rrd)
  else()
    vlink_install_completions(${PROJECT_NAME})
  endif()
endif()
