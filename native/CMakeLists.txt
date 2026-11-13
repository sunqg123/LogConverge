cmake_minimum_required(VERSION 3.21)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Use an out-of-source build, for example: cmake --preset windows-debug")
endif()

project(
    LogConverge
    VERSION 0.7.0
    DESCRIPTION "LogConverge native logging SDK"
    LANGUAGES CXX
)

option(BUILD_SHARED_LIBS "Build shared libraries" ON)

# 固定依赖版本和校验值，第三方类型不进入公共接口。
include(FetchContent)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
set(spdlog_archive "https://codeload.github.com/gabime/spdlog/tar.gz/refs/tags/v1.15.3")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/build/dependency-cache/spdlog-1.15.3.tar.gz")
  set(spdlog_archive "${CMAKE_CURRENT_SOURCE_DIR}/build/dependency-cache/spdlog-1.15.3.tar.gz")
endif()
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spdlog
    URL "${spdlog_archive}"
    URL_HASH SHA256=15a04e69c222eb6c01094b5c7ff8a249b36bb22788d72519646fb85feb267e67)
FetchContent_MakeAvailable(spdlog)
set_target_properties(spdlog PROPERTIES EXCLUDE_FROM_ALL TRUE)

# ---- Declare library ----

add_library(
    LogConverge_LogConverge
    source/Api.cpp
    source/Logger.cpp
    source/FileSink.cpp
)
add_library(LogConverge::LogConverge ALIAS LogConverge_LogConverge)

include(GenerateExportHeader)
generate_export_header(
    LogConverge_LogConverge
    BASE_NAME LogConverge
    EXPORT_FILE_NAME export/LogConverge/LogConverge_export.hpp
)

if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(LogConverge_LogConverge PUBLIC LOGCONVERGE_STATIC_DEFINE)
endif()

set_target_properties(
    LogConverge_LogConverge PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
    EXPORT_NAME LogConverge
    OUTPUT_NAME LogConverge
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_include_directories(
    LogConverge_LogConverge
    PUBLIC
    "\$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
)

target_include_directories(
    LogConverge_LogConverge SYSTEM
    PUBLIC
    "\$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/export>"
)

target_compile_features(LogConverge_LogConverge PUBLIC cxx_std_17)
include(cmake/native-output.cmake)
find_package(Threads REQUIRED)
target_link_libraries(LogConverge_LogConverge PRIVATE Threads::Threads
    "$<BUILD_INTERFACE:spdlog::spdlog_header_only>")
if(WIN32)
  target_compile_definitions(LogConverge_LogConverge PRIVATE SPDLOG_WCHAR_FILENAMES)
endif()
if(MSVC)
  target_compile_options(LogConverge_LogConverge PRIVATE /utf-8)
endif()

option(LOGCONVERGE_BUILD_EXAMPLES "Build the C++ usage example" OFF)
if(LOGCONVERGE_BUILD_EXAMPLES)
  add_executable(LogConvergeExample examples/Basic.cpp)
  target_link_libraries(LogConvergeExample PRIVATE LogConverge::LogConverge)
  set_target_properties(LogConvergeExample PROPERTIES
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  if(MSVC)
    target_compile_options(LogConvergeExample PRIVATE /utf-8)
  endif()
  if(WIN32 AND BUILD_SHARED_LIBS)
    # Windows 示例从自身目录加载 DLL。
    add_custom_command(TARGET LogConvergeExample POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:LogConverge_LogConverge>" "$<TARGET_FILE_DIR:LogConvergeExample>"
        VERBATIM)
  endif()
endif()

# ---- Install rules ----

if(NOT CMAKE_SKIP_INSTALL_RULES)
  include(cmake/install-rules.cmake)
endif()

