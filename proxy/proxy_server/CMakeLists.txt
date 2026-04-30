#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

set(VLINK_MODULE_NAME proxy_server)
set(VLINK_MODULE_IMPORT_ALL TRUE)

project(vlink-${VLINK_MODULE_NAME} LANGUAGES CXX)

# src
add_library(${PROJECT_NAME} ../proxy_macros.h proxy_server.cc)
add_library(${PROJECT_NAMESPACE}${PROJECT_NAME} ALIAS ${PROJECT_NAME})
add_library(${PROJECT_NAMESPACE}${VLINK_MODULE_NAME} ALIAS ${PROJECT_NAME})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(${PROJECT_NAME} PUBLIC vlink::vlink vlink::all)
target_link_libraries(${PROJECT_NAME} PRIVATE $<BUILD_INTERFACE:proxy_gen> ${protobuf_libs})
target_compile_definitions(${PROJECT_NAME} PUBLIC "VLINK_ENABLE_PROXY")
if(BUILD_SHARED_LIBS)
  target_compile_definitions(${PROJECT_NAME} PRIVATE "VLINK_PROXY_SERVER_LIBRARY")
else()
  target_compile_definitions(${PROJECT_NAME} PUBLIC "VLINK_PROXY_SERVER_LIBRARY_STATIC")
endif()

# install
install(
  TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# export
vlink_export(${PROJECT_NAME} ${PROJECT_NAME} ${PROJECT_NAMESPACE} ${CMAKE_PROJECT_VERSION})
