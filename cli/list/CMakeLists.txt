#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

project(vlink-list LANGUAGES CXX)

# src
add_executable(${PROJECT_NAME} list.cc)
target_link_libraries(${PROJECT_NAME} PRIVATE vlink::vlink ${VLINK_NO_INTRA_LIBRARIES})
target_link_libraries(${PROJECT_NAME} PRIVATE thirdparty::argparse)

if(WIN32)
  if(EXISTS ${CMAKE_SOURCE_DIR}/tools/ico/vlink-cmd.rc)
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/tools/ico/vlink-cmd.rc)
  endif()
endif()

# install
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
vlink_install_symlink(list SYMLINK ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})

# completions
vlink_install_completions(${PROJECT_NAME} ALIASES list)
