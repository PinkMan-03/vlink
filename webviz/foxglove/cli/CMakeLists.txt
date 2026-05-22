#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

project(vlink-bag2mcap LANGUAGES CXX)

add_executable(${PROJECT_NAME} bag2mcap.cc)

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/../src ${PROJECT_SOURCE_DIR}/../..)

target_sources(
  ${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/../src/foxglove_converter.cc
                          ${PROJECT_SOURCE_DIR}/../src/foxglove_converter.h
)

target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE vlink::vlink
          vlink::proxy_api
          ${protobuf_libs}
          ${flatbuffers_libs}
          thirdparty::json
          thirdparty::mcap
          thirdparty::argparse
          thirdparty::exprtk
          $<BUILD_INTERFACE:foxglove_fbs_gen>
)

install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# symlinks
if(ENABLE_SYMLINKS)
  vlink_install_symlink(bag2mcap SYMLINK ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# completions
if(ENABLE_COMPLETIONS)
  vlink_install_completions(${PROJECT_NAME} ALIAS bag2mcap)
endif()
