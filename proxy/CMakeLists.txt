#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

project(vlink-proxy)

# proto
if(TARGET protobuf::libprotobuf)
  set(protobuf_libs protobuf::libprotobuf)
elseif(TARGET protobuf::libprotobuf-lite)
  set(protobuf_libs protobuf::libprotobuf-lite)
endif()
vlink_generate_cpp(TARGET proxy_gen PROTO ${PROJECT_SOURCE_DIR}/proxy.proto)

# proxy_api
add_subdirectory(proxy_api)

# proxy_server
add_subdirectory(proxy_server)

# proxy_app
add_executable(${PROJECT_NAME} proxy_macros.h proxy.cc)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${PROJECT_NAME} PRIVATE vlink::vlink vlink::all)
target_link_libraries(${PROJECT_NAME} PRIVATE thirdparty::argparse vlink::proxy_server)

if(WIN32)
  if(EXISTS ${CMAKE_SOURCE_DIR}/tools/ico/vlink-cmd.rc)
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/tools/ico/vlink-cmd.rc)
  endif()
endif()

# copy
add_custom_command(
  TARGET ${PROJECT_NAME}
  POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/etc/
          ${CMAKE_BINARY_DIR}/output/${BUILD_CONFIG_DIR}/vlink-proxy/
)

# install
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# symlinks
if(ENABLE_SYMLINKS)
  vlink_install_symlink(proxy SYMLINK ${PROJECT_NAME} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# completions
if(ENABLE_COMPLETIONS)
  vlink_install_completions(${PROJECT_NAME} ALIAS proxy)
endif()

# etc
if(TARGET vlink-shm AND EXISTS ${PROJECT_SOURCE_DIR}/etc)
  install(
    DIRECTORY ${PROJECT_SOURCE_DIR}/etc/
    DESTINATION ${INSTALL_CONFIG_DIR}/vlink-proxy/
    REGEX "completions" EXCLUDE
  )
endif()
