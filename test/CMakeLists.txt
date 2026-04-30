#
# Copyright (C) 2026 by Thun Lu. All rights reserved.
#

project(vlink-test)

# src
file(GLOB_RECURSE PROJECT_HDRS ${PROJECT_SOURCE_DIR}/*.h)
file(GLOB_RECURSE PROJECT_SRCS ${PROJECT_SOURCE_DIR}/*.cc ${PROJECT_SOURCE_DIR}/*.cxx)
add_executable(${PROJECT_NAME} ${PROJECT_HDRS} ${PROJECT_SRCS})
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/idl)
target_link_libraries(${PROJECT_NAME} PRIVATE vlink::vlink vlink::all thirdparty::doctest)
target_compile_definitions(${PROJECT_NAME} PRIVATE ${VLINK_APP_DEFINES})
target_compile_definitions(${PROJECT_NAME} PRIVATE "DOCTEST_CONFIG_USE_STD_HEADERS")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_CLANG_TIDY "")

if(WIN32)
  if(EXISTS ${CMAKE_SOURCE_DIR}/tools/ico/vlink-cmd.rc)
    target_sources(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/tools/ico/vlink-cmd.rc)
  endif()
endif()

if(ENABLE_SECURITY)
  target_compile_definitions(${PROJECT_NAME} PRIVATE "VLINK_TEST_SUPPORT_SECURITY")
endif()

# test
add_test(NAME ${PROJECT_NAME} COMMAND ${PROJECT_NAME})

if(UNIX AND NOT APPLE)
  set_tests_properties(
    ${PROJECT_NAME} PROPERTIES ENVIRONMENT "LD_LIBRARY_PATH=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}:$ENV{LD_LIBRARY_PATH}"
  )
elseif(APPLE)
  set_tests_properties(
    ${PROJECT_NAME} PROPERTIES ENVIRONMENT
                               "DYLD_LIBRARY_PATH=${CMAKE_LIBRARY_OUTPUT_DIRECTORY}:$ENV{DYLD_LIBRARY_PATH}"
  )
endif()

# requires
if(NOT Protobuf_FOUND)
  find_package(Protobuf CONFIG QUIET)
endif()

if(NOT Flatbuffers_FOUND)
  find_package(Flatbuffers CONFIG QUIET)
endif()

set(ENABLE_FASTDDS_GEN_TEST OFF)

# idl(fastdds-gen)
if(ENABLE_FASTDDS_GEN_TEST AND TARGET vlink-dds)
  find_program(FASTDDS_GEN_EXECUTABLE NAMES fastddsgen)
  if(FASTDDS_GEN_EXECUTABLE)
    file(GLOB IDL_SRCS ${PROJECT_SOURCE_DIR}/idl/*.idl)
    vlink_generate_cpp(TARGET sample_gen_fastdds DDS ${IDL_SRCS})
    target_link_libraries(${PROJECT_NAME} PRIVATE sample_gen_fastdds)
    target_compile_definitions(${PROJECT_NAME} PRIVATE "VLINK_TEST_SUPPORT_FASTDDS_GEN")
  endif()
endif()

if(Protobuf_FOUND)
  file(GLOB IDL_SRCS ${PROJECT_SOURCE_DIR}/idl/*.proto)
  vlink_generate_cpp(TARGET sample_gen_protobuf PROTO ${IDL_SRCS})
  target_link_libraries(${PROJECT_NAME} PRIVATE sample_gen_protobuf)
  target_compile_definitions(${PROJECT_NAME} PRIVATE "VLINK_TEST_SUPPORT_PROTOBUF")
endif()

if(Flatbuffers_FOUND)
  file(GLOB IDL_SRCS ${PROJECT_SOURCE_DIR}/idl/*.fbs)
  vlink_generate_cpp(TARGET sample_gen_flatbuffers FBS ${IDL_SRCS})
  target_link_libraries(${PROJECT_NAME} PRIVATE sample_gen_flatbuffers)
  target_compile_definitions(${PROJECT_NAME} PRIVATE "VLINK_TEST_SUPPORT_FLATBUFFERS")
endif()

# install
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
