cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

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

target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${PLUGIN_SOURCES})

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
# Standalone test executable for libnode experiments
# ============================================================================
option(BUILD_LIBNODE_TEST "Build standalone libnode test executable" ON)

if(BUILD_LIBNODE_TEST)
  add_executable(test-libnode src/test-libnode.cpp)
  
  set_target_properties(test-libnode PROPERTIES
    CXX_STANDARD 20 # C++20 or later required for node 24.3.0
    CXX_STANDARD_REQUIRED ON
  )
  
  target_link_libraries(test-libnode PRIVATE LibNode::LibNode)
  
  # Copy libnode DLL and PDB for test executable
  if(WIN32 AND LibNode_DLL)
    add_custom_command(
      TARGET test-libnode
      POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibNode_DLL}" "$<TARGET_FILE_DIR:test-libnode>"
      COMMENT "Copying libnode.dll to test-libnode output"
    )
    if(LibNode_PDB)
      add_custom_command(
        TARGET test-libnode
        POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${LibNode_PDB}" "$<TARGET_FILE_DIR:test-libnode>"
        COMMENT "Copying libnode.pdb to test-libnode output"
      )
    endif()
  endif()
endif()
