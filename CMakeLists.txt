cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)
include(cpm) # Out CMake Package Manager

#CPMAddPackage("gh:fmtlib/fmt#12.1.0")
CPMAddPackage(
  URI "gh:nlohmann/json@3.12.0"
  OPTIONS "JSON_BuildTests OFF"
)
#CPMAddPackage("gh:Curve/channel#v3.0.1")
#CPMAddPackage("gh:Curve/lockpp#v3.2.0")
#CPMAddPackage("gh:Curve/flagpp#v3.1.0")
#CPMAddPackage("gh:Curve/ereignis#v6.2.0")
#CPMAddPackage("gh:Curve/poolparty#v3.0.1")
#CPMAddPackage("gh:Curve/lambda#v1.0")
#CPMAddPackage("gh:Curve/polo#v1.0.1")
#CPMAddPackage("gh:Curve/coco#v4.2.0")
#CPMAddPackage("gh:Curve/rebind#v5.3.1")
#CPMAddPackage("gh:Curve/eraser#v2.3.0")

# Define NOMINMAX on Windows to prevent min/max macro conflicts
if(WIN32)
  add_compile_definitions(NOMINMAX)
endif()

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
#  fmt::fmt
  nlohmann_json::nlohmann_json
)
#target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
#  lambda::ptr
#  cr::ereignis
#  cr::lockpp
#  cr::channel
#  cr::coco
#  cr::polo
#  cr::rebind
#  cr::eraser
#  cr::flagpp
#  cr::poolparty
#)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

file(GLOB_RECURSE PLUGIN_SOURCES
    src/*.c
    src/*.cc
    src/*.cpp
)
# Exclude test executable from plugin sources
list(FILTER PLUGIN_SOURCES EXCLUDE REGEX "test-.*\\.cpp$")
list(FILTER PLUGIN_SOURCES EXCLUDE REGEX "test-.*\\.cc$")
list(FILTER PLUGIN_SOURCES EXCLUDE REGEX "test-.*\\.c$")

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${PLUGIN_SOURCES})
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  resources.qrc
)

# Enable C++20 for Node.js embedding API (required by Node.js 24.x)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
)

# Find and link libnode
find_package(LibNode REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE LibNode::LibNode)

# Copy libnode DLL and PDB to output directory on Windows
if(WIN32 AND LibNode_DLL)
  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibNode_DLL}" "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>"
    COMMENT "Copying libnode.dll to output directory"
  )
  if(LibNode_PDB)
    add_custom_command(
      TARGET ${CMAKE_PROJECT_NAME}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibNode_PDB}" "$<TARGET_FILE_DIR:${CMAKE_PROJECT_NAME}>"
      COMMENT "Copying libnode.pdb to output directory"
    )
  endif()
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# ============================================================================
# Deploy to OBS plugins directory (based on OBS_PLUGINS_DIR env var)
# ============================================================================
if(DEFINED ENV{OBS_PLUGINS_DIR})
  set(OBS_PLUGINS_DIR $ENV{OBS_PLUGINS_DIR})
  message(STATUS "OBS_PLUGINS_DIR: ${OBS_PLUGINS_DIR}")
  
  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OBS_PLUGINS_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${CMAKE_PROJECT_NAME}>" "${OBS_PLUGINS_DIR}"
    COMMENT "Deploying plugin to OBS plugins directory: ${OBS_PLUGINS_DIR}"
  )
  
  # Copy plugin PDB if it exists
  add_custom_command(
    TARGET ${CMAKE_PROJECT_NAME}
    POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PDB_FILE:${CMAKE_PROJECT_NAME}>" "${OBS_PLUGINS_DIR}"
    COMMENT "Deploying plugin PDB to OBS plugins directory"
  )
  
  # Copy libnode DLL and PDB
  if(WIN32 AND LibNode_DLL)
    add_custom_command(
      TARGET ${CMAKE_PROJECT_NAME}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibNode_DLL}" "${OBS_PLUGINS_DIR}"
      COMMENT "Deploying libnode.dll to OBS plugins directory"
    )
    if(LibNode_PDB)
      add_custom_command(
        TARGET ${CMAKE_PROJECT_NAME}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibNode_PDB}" "${OBS_PLUGINS_DIR}"
        COMMENT "Deploying libnode.pdb to OBS plugins directory"
      )
    endif()
  endif()
else()
  message(STATUS "OBS_PLUGINS_DIR not set - skipping deployment to OBS")
endif()

# ============================================================================
# Testing with CTest and Catch2
# ============================================================================
option(BUILD_TESTS "Build tests" ON)
option(BUILD_LEGACY_TESTS "Build standalone libnode test executable" ON)
option(BUILD_BENCHMARKS "Build benchmark tests" ON)

file(GLOB_RECURSE EXPERIMENT_SOURCES
    src/experiments/*.c
    src/experiments/*.cc
    src/experiments/*.cpp
)

if(BUILD_TESTS OR BUILD_BENCHMARKS)
  enable_testing()

  # test dependencies
  CPMAddPackage("gh:catchorg/Catch2@3.11.0")

  # Include Catch2's CMake helpers for test discovery
  # for the below included Catch cmake module
  list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)

  include(CTest)
  include(Catch) # for: catch_discover_tests()
endif()

# Old standalone libnode test (if exists)
if(BUILD_LEGACY_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy/test-libnode.cpp")
  set(LIBNODE_SIMPLE_TEST "test-libnode-simple")

  add_executable(${LIBNODE_SIMPLE_TEST} ${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy/test-libnode.cpp)
  target_include_directories(${LIBNODE_SIMPLE_TEST} PRIVATE src)

  set_target_properties(${LIBNODE_SIMPLE_TEST} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
  )

  target_link_libraries(${LIBNODE_SIMPLE_TEST} PRIVATE LibNode::LibNode)

  # Copy libnode DLL and PDB for test executable
  if(WIN32 AND LibNode_DLL)
    add_custom_command(
      TARGET ${LIBNODE_SIMPLE_TEST}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibNode_DLL}" "$<TARGET_FILE_DIR:${LIBNODE_SIMPLE_TEST}>"
      COMMENT "Copying libnode.dll to ${LIBNODE_SIMPLE_TEST} output"
    )
    if(LibNode_PDB)
      add_custom_command(
        TARGET ${LIBNODE_SIMPLE_TEST}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibNode_PDB}" "$<TARGET_FILE_DIR:${LIBNODE_SIMPLE_TEST}>"
        COMMENT "Copying libnode.pdb to ${LIBNODE_SIMPLE_TEST} output"
      )
    endif()
  endif()
endif()

# Legacy multi test (if exists)
if(BUILD_LEGACY_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy/test-libnode-multi.cc")
  set(LIBNODE_MULTI_TEST "test-libnode-multi")

  add_executable(${LIBNODE_MULTI_TEST} ${CMAKE_CURRENT_SOURCE_DIR}/tests/legacy/test-libnode-multi.cc)
  target_sources(${LIBNODE_MULTI_TEST} PRIVATE ${EXPERIMENT_SOURCES})
  target_include_directories(${LIBNODE_MULTI_TEST} PRIVATE src)

  set_target_properties(${LIBNODE_MULTI_TEST} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
  )

  target_link_libraries(${LIBNODE_MULTI_TEST} PRIVATE LibNode::LibNode)

  # Copy libnode DLL and PDB for test executable
  if(WIN32 AND LibNode_DLL)
    add_custom_command(
      TARGET ${LIBNODE_MULTI_TEST}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibNode_DLL}" "$<TARGET_FILE_DIR:${LIBNODE_MULTI_TEST}>"
      COMMENT "Copying libnode.dll to ${LIBNODE_MULTI_TEST} output"
    )
    if(LibNode_PDB)
      add_custom_command(
        TARGET ${LIBNODE_MULTI_TEST}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibNode_PDB}" "$<TARGET_FILE_DIR:${LIBNODE_MULTI_TEST}>"
        COMMENT "Copying libnode.pdb to ${LIBNODE_MULTI_TEST} output"
      )
    endif()
  endif()
endif()

# ============================================================================
# Main Experiments Test Suite
# ============================================================================
if(BUILD_TESTS)
  set(LIBNODE_TEST_TARGET "test-libnode-experiments")

  add_executable(${LIBNODE_TEST_TARGET} tests/test_experiments.cpp)
  target_sources(${LIBNODE_TEST_TARGET} PRIVATE ${EXPERIMENT_SOURCES})
  target_include_directories(${LIBNODE_TEST_TARGET} PRIVATE src)

  set_target_properties(${LIBNODE_TEST_TARGET} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
  )

  target_link_libraries(${LIBNODE_TEST_TARGET} PRIVATE
    LibNode::LibNode
    Catch2::Catch2 # Testing Library
#    Catch2::Catch2WithMain # Testing Library
  )
  target_link_libraries(${LIBNODE_TEST_TARGET} PRIVATE
    #  fmt::fmt
    nlohmann_json::nlohmann_json
  )

  # Copy libnode DLL and PDB for test executable
  if(WIN32 AND LibNode_DLL)
    add_custom_command(
      TARGET ${LIBNODE_TEST_TARGET}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibNode_DLL}" "$<TARGET_FILE_DIR:${LIBNODE_TEST_TARGET}>"
      COMMENT "Copying libnode.dll to ${LIBNODE_TEST_TARGET} output"
    )
    if(LibNode_PDB)
      add_custom_command(
        TARGET ${LIBNODE_TEST_TARGET}
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibNode_PDB}" "$<TARGET_FILE_DIR:${LIBNODE_TEST_TARGET}>"
        COMMENT "Copying libnode.pdb to ${LIBNODE_TEST_TARGET} output"
      )
    endif()
  endif()

#  catch_discover_tests(${LIBNODE_TEST_TARGET})
endif()

# ============================================================================
# Fuzzing Tests
# ============================================================================
if(BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_fuzzing.cpp")
  set(LIBNODE_FUZZING_TARGET "test-libnode-fuzzing")
  
  add_executable(${LIBNODE_FUZZING_TARGET} tests/test_fuzzing.cpp)
  target_sources(${LIBNODE_FUZZING_TARGET} PRIVATE ${EXPERIMENT_SOURCES})
  target_include_directories(${LIBNODE_FUZZING_TARGET} PRIVATE src)
  
  set_target_properties(${LIBNODE_FUZZING_TARGET} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
  )
  
  target_link_libraries(${LIBNODE_FUZZING_TARGET} PRIVATE
    LibNode::LibNode
    Catch2::Catch2 # Testing Library
    # Catch2::Catch2WithMain # Testing Library
  )
  target_link_libraries(${LIBNODE_FUZZING_TARGET} PRIVATE
    #  fmt::fmt
    nlohmann_json::nlohmann_json
  )
  
  # Copy libnode DLL
  if(WIN32 AND LibNode_DLL)
    add_custom_command(
      TARGET ${LIBNODE_FUZZING_TARGET}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibNode_DLL}" "$<TARGET_FILE_DIR:${LIBNODE_FUZZING_TARGET}>"
      COMMENT "Copying libnode.dll to ${LIBNODE_FUZZING_TARGET} output"
    )
  endif()
  
  catch_discover_tests(${LIBNODE_FUZZING_TARGET})
endif()

# ============================================================================
# Benchmarks (optional)
# ============================================================================
if(BUILD_BENCHMARKS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/benchmarks.cpp")
  set(LIBNODE_BENCHMARK_TARGET "test-libnode-benchmarks")
  
  add_executable(${LIBNODE_BENCHMARK_TARGET} tests/benchmarks.cpp)
  target_sources(${LIBNODE_BENCHMARK_TARGET} PRIVATE ${EXPERIMENT_SOURCES})
  target_include_directories(${LIBNODE_BENCHMARK_TARGET} PRIVATE src)
  
  set_target_properties(${LIBNODE_BENCHMARK_TARGET} PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
  )
  
  target_link_libraries(${LIBNODE_BENCHMARK_TARGET} PRIVATE
    LibNode::LibNode
    Catch2::Catch2 # Testing Library
    # Catch2::Catch2WithMain # Testing Library
  )
  target_link_libraries(${LIBNODE_BENCHMARK_TARGET} PRIVATE
    #  fmt::fmt
    nlohmann_json::nlohmann_json
  )
  
  # Copy libnode DLL
  if(WIN32 AND LibNode_DLL)
    add_custom_command(
      TARGET ${LIBNODE_BENCHMARK_TARGET}
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibNode_DLL}" "$<TARGET_FILE_DIR:${LIBNODE_BENCHMARK_TARGET}>"
      COMMENT "Copying libnode.dll to ${LIBNODE_BENCHMARK_TARGET} output"
    )
  endif()
  
  # Note: Benchmarks are not auto-discovered as tests since they're for performance measurement
  message(STATUS "Benchmarks enabled - run ${LIBNODE_BENCHMARK_TARGET} manually")
endif()
