# A642A64: AArch64 Stencil-Based Interpreter Library
cmake_minimum_required(VERSION 3.15)

set(A642A64_SOURCES
  src/ppc_stencils_generated.cpp
  src/Interpreter.cpp
)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
  enable_language(ASM)
  list(APPEND A642A64_SOURCES src/Interpreter.S)
endif()

add_library(A642A64 STATIC ${A642A64_SOURCES})

target_include_directories(A642A64 PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_features(A642A64 PUBLIC cxx_std_20)
target_compile_options(A642A64 PRIVATE -w -Wno-error)
