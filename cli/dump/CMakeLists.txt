#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

project(vlink-dump LANGUAGES CXX)

# requires
if(NOT TARGET protobuf::libprotobuf)
  find_package(Protobuf CONFIG QUIET)
  if(NOT TARGET protobuf::libprotobuf)
    find_package(Protobuf REQUIRED)
  endif()
endif()

# src
add_executable(${PROJECT_NAME} dump.cc)
target_link_libraries(${PROJECT_NAME} PRIVATE vlink::vlink ${VLINK_NO_INTRA_LIBRARIES})
target_link_libraries(${PROJECT_NAME} PRIVATE thirdparty::argparse thirdparty::json protobuf::libprotobuf)

if(TARGET thirdparty::exprtk)
  target_link_libraries(${PROJECT_NAME} PRIVATE thirdparty::exprtk)
endif()

if(TARGET flatbuffers::flatbuffers_shared)
  target_link_libraries(${PROJECT_NAME} PRIVATE flatbuffers::flatbuffers_shared)
elseif(TARGET flatbuffers::flatbuffers)
  target_link_libraries(${PROJECT_NAME} PRIVATE flatbuffers::flatbuffers)
endif()

if(WIN32)
  if(EXISTS ${CMAKE_SOURCE_DIR}/tools/ico/vlink-cmd.rc)
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/tools/ico/vlink-cmd.rc)
  endif()
endif()

# install
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# symlinks
if(ENABLE_SYMLINKS)
  vlink_install_symlink(dump SYMLINK ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# completions
if(ENABLE_COMPLETIONS)
  vlink_install_completions(${PROJECT_NAME} ALIAS dump)
endif()
