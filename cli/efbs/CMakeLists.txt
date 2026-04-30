#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

project(vlink-efbs LANGUAGES CXX)

# requires
if(NOT TARGET flatbuffers::flatbuffers_shared AND NOT TARGET flatbuffers::flatbuffers)
  find_package(Flatbuffers CONFIG REQUIRED)
endif()

if(DEFINED Flatbuffers_VERSION)
  if(Flatbuffers_VERSION VERSION_LESS "22.0.0")
    message(WARNING "Flatbuffers version too old, skip build efbs")
    return()
  endif()
endif()

# src
add_executable(${PROJECT_NAME} efbs.cc)
target_link_libraries(${PROJECT_NAME} PRIVATE vlink::vlink ${VLINK_NO_INTRA_LIBRARIES})
target_link_libraries(${PROJECT_NAME} PRIVATE thirdparty::argparse thirdparty::json protobuf::libprotobuf)
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
vlink_install_symlink(efbs SYMLINK ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})

# completions
vlink_install_completions(${PROJECT_NAME} ALIASES efbs)
