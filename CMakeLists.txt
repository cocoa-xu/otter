cmake_minimum_required(VERSION 3.18.0)
project(otter DESCRIPTION "use this cmake to generate compiler db JSON file")

set(CMAKE_INSTALL_MESSAGE LAZY CACHE STRING "" FORCE)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "" FORCE)

# Find ERTS
find_program(ELIXIR_EXECUTABLE NAMES elixir REQUIRED)
execute_process(
  COMMAND ${ELIXIR_EXECUTABLE} --eval ":code.root_dir() |> IO.puts()"
  OUTPUT_VARIABLE ERTS_DIR
  COMMAND_ERROR_IS_FATAL ANY
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
  COMMAND ${ELIXIR_EXECUTABLE} --eval ":erlang.system_info(:version) |> IO.puts()"
  OUTPUT_VARIABLE ERTS_VERSION
  COMMAND_ERROR_IS_FATAL ANY
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
  COMMAND ${ELIXIR_EXECUTABLE} --eval ":code.lib_dir(:erl_interface, :include) |> IO.puts()"
  OUTPUT_VARIABLE ERL_INTERFACE_INCLUDE_DIR
  COMMAND_ERROR_IS_FATAL ANY
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
execute_process(
  COMMAND ${ELIXIR_EXECUTABLE} --eval ":code.lib_dir(:erl_interface, :lib) |> IO.puts()"
  OUTPUT_VARIABLE ERL_INTERFACE_LIB_DIR
  COMMAND_ERROR_IS_FATAL ANY
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
message(STATUS "ERTS_DIR: ${ERTS_DIR}")
find_path(ERTS_INCLUDE_DIR erl_nif.h HINTS ${ERTS_DIR} PATH_SUFFIXES usr/include erts-${ERTS_VERSION}/include)
message(STATUS "ERTS_INCLUDE_DIR: ${ERTS_INCLUDE_DIR}")
message(STATUS "ERL_INTERFACE_INCLUDE_DIR: ${ERL_INTERFACE_INCLUDE_DIR}")
message(STATUS "ERL_INTERFACE_LIB_DIR: ${ERL_INTERFACE_LIB_DIR}")
find_library(LIB_EI NAMES ei HINTS ${ERL_INTERFACE_LIB_DIR} REQUIRED)
message(STATUS "LIB_EI: ${LIB_EI}")
add_library(erl_interface UNKNOWN IMPORTED ${LIB_EI})
set_target_properties(erl_interface PROPERTIES IMPORTED_LOCATION ${LIB_EI})

include(cmake/FindFFI.cmake)
if (FFI_FOUND)
  message(STATUS "FFI_INCLUDE_DIRS: ${FFI_INCLUDE_DIRS}")
  message(STATUS "FFI_LIBRARIES: ${FFI_LIBRARIES}")
  message(STATUS "HAVE_FFI_CALL: ${HAVE_FFI_CALL}")
else()
  message(FATAL_ERROR "FFI not found: ${FFI_FOUND}")
endif()

include_directories(${ERTS_INCLUDE_DIR})
include_directories(${nifpp_SOURCE_DIR})
include_directories(${FFI_INCLUDE_DIRS}/ffi)

set(CMAKE_SHARED_LIBRARY_SUFFIX .so)

add_library(otter_nif SHARED
    c_src/otter_nif.cpp
)
target_link_libraries(otter_nif FFI::ffi)
