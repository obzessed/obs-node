# FindLibNode.cmake
# CMake module to find and configure the libnode (Node.js shared library)
#
# This module defines:
#   LibNode_FOUND        - True if libnode was found
#   LibNode_INCLUDE_DIRS - Include directories for Node.js headers
#   LibNode_LIBRARIES    - Libraries to link against
#   LibNode_DLL          - Path to the DLL file (Windows only)
#   LibNode_PDB          - Path to the PDB file (Windows only)
#   LibNode_VERSION      - Version of libnode

include(FindPackageHandleStandardArgs)

# Read libnode.json for version info
set(LIBNODE_JSON_PATH "${CMAKE_SOURCE_DIR}/libnode.json")
if(EXISTS "${LIBNODE_JSON_PATH}")
  file(READ "${LIBNODE_JSON_PATH}" LIBNODE_JSON)
  string(JSON LibNode_VERSION GET ${LIBNODE_JSON} libnode version)
  string(JSON LIBNODE_BASE_URL GET ${LIBNODE_JSON} libnode baseUrl)
else()
  message(WARNING "libnode.json not found at ${LIBNODE_JSON_PATH}")
endif()

# Platform-specific settings
if(WIN32)
  set(LIBNODE_PLATFORM "amd64-windows")
#  set(LIBNODE_ARCHIVE_EXT "tar.xz")
  set(LIBNODE_ARCHIVE_EXT "zip")
  set(LIBNODE_LIB_NAME "libnode.lib")
  set(LIBNODE_DLL_NAME "libnode.dll")
elseif(APPLE)
  set(LIBNODE_PLATFORM "amd64-macos")
  set(LIBNODE_ARCHIVE_EXT "tar.xz")
  set(LIBNODE_LIB_NAME "libnode.dylib")
else()
  set(LIBNODE_PLATFORM "amd64-linux")
  set(LIBNODE_ARCHIVE_EXT "tar.xz")
  set(LIBNODE_LIB_NAME "libnode.so")
endif()

# Set root directories
set(LIBNODE_ROOT "${CMAKE_SOURCE_DIR}/.deps/libnode-${LibNode_VERSION}")
set(LIBNODE_HEADERS_ROOT "${CMAKE_SOURCE_DIR}/.deps/node-v${LibNode_VERSION}-headers")

# Download URL for libnode binaries (metacall)
set(LIBNODE_DOWNLOAD_URL "${LIBNODE_BASE_URL}/v${LibNode_VERSION}/libnode-${LIBNODE_PLATFORM}.${LIBNODE_ARCHIVE_EXT}")

# Download URL for Node.js headers (official)
set(NODE_HEADERS_URL "https://nodejs.org/dist/v${LibNode_VERSION}/node-v${LibNode_VERSION}-headers.tar.gz")

# ============================================================================
# Download and extract libnode binaries
# ============================================================================
set(LIBNODE_ARCHIVE "${CMAKE_SOURCE_DIR}/.deps/libnode-${LibNode_VERSION}.${LIBNODE_ARCHIVE_EXT}")

# Get expected hash from libnode.json
if(WIN32)
  string(JSON LIBNODE_EXPECTED_HASH ERROR_VARIABLE _hash_error GET ${LIBNODE_JSON} libnode hashes windows-x64)
elseif(APPLE)
  string(JSON LIBNODE_EXPECTED_HASH ERROR_VARIABLE _hash_error GET ${LIBNODE_JSON} libnode hashes macos-x64)
else()
  string(JSON LIBNODE_EXPECTED_HASH ERROR_VARIABLE _hash_error GET ${LIBNODE_JSON} libnode hashes linux-x64)
endif()

if(NOT EXISTS "${LIBNODE_ROOT}")
  set(_need_download TRUE)
  set(_need_extract TRUE)
  
  # Check if archive already exists
  if(EXISTS "${LIBNODE_ARCHIVE}")
    message(STATUS "Found existing archive: ${LIBNODE_ARCHIVE}")
    
    # Verify checksum if we have an expected hash
    if(LIBNODE_EXPECTED_HASH AND NOT _hash_error)
      file(SHA256 "${LIBNODE_ARCHIVE}" _archive_hash)
      if(_archive_hash STREQUAL LIBNODE_EXPECTED_HASH)
        message(STATUS "Archive checksum valid, skipping download")
        set(_need_download FALSE)
      else()
        message(STATUS "Archive checksum mismatch:")
        message(STATUS "  Expected: ${LIBNODE_EXPECTED_HASH}")
        message(STATUS "  Got:      ${_archive_hash}")
        message(STATUS "Re-downloading...")
        file(REMOVE "${LIBNODE_ARCHIVE}")
      endif()
    else()
      message(STATUS "No expected hash configured, using existing archive")
      set(_need_download FALSE)
    endif()
  endif()
  
  # Download if needed
  if(_need_download)
    message(STATUS "Downloading libnode from ${LIBNODE_DOWNLOAD_URL}")
    
    if(LIBNODE_EXPECTED_HASH AND NOT _hash_error)
      # Download with checksum verification
      file(DOWNLOAD "${LIBNODE_DOWNLOAD_URL}" "${LIBNODE_ARCHIVE}"
        STATUS DOWNLOAD_STATUS
        SHOW_PROGRESS
        EXPECTED_HASH SHA256=${LIBNODE_EXPECTED_HASH}
      )
    else()
      # Download without checksum
      file(DOWNLOAD "${LIBNODE_DOWNLOAD_URL}" "${LIBNODE_ARCHIVE}"
        STATUS DOWNLOAD_STATUS
        SHOW_PROGRESS
      )
    endif()
    
    list(GET DOWNLOAD_STATUS 0 DOWNLOAD_RESULT)
    if(NOT DOWNLOAD_RESULT EQUAL 0)
      message(FATAL_ERROR "Failed to download libnode: ${DOWNLOAD_STATUS}")
    endif()
  endif()
  
  # Extract
  if(_need_extract)
    message(STATUS "Extracting libnode to ${LIBNODE_ROOT}")
    file(MAKE_DIRECTORY "${LIBNODE_ROOT}")
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E tar xf "${LIBNODE_ARCHIVE}"
      WORKING_DIRECTORY "${LIBNODE_ROOT}"
      RESULT_VARIABLE EXTRACT_RESULT
    )
    if(NOT EXTRACT_RESULT EQUAL 0)
      message(FATAL_ERROR "Failed to extract libnode archive")
    endif()
  endif()
endif()


# ============================================================================
# Download and extract Node.js headers
# ============================================================================
if(NOT EXISTS "${LIBNODE_HEADERS_ROOT}")
  message(STATUS "Node.js headers not found, downloading from ${NODE_HEADERS_URL}")
  
  set(HEADERS_ARCHIVE "${CMAKE_SOURCE_DIR}/.deps/node-v${LibNode_VERSION}-headers.tar.gz")
  
  # Download
  file(DOWNLOAD "${NODE_HEADERS_URL}" "${HEADERS_ARCHIVE}"
    STATUS DOWNLOAD_STATUS
    SHOW_PROGRESS
  )
  list(GET DOWNLOAD_STATUS 0 DOWNLOAD_RESULT)
  if(NOT DOWNLOAD_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to download Node.js headers: ${DOWNLOAD_STATUS}")
  endif()
  
  # Extract to .deps directory
  message(STATUS "Extracting Node.js headers")
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E tar xf "${HEADERS_ARCHIVE}"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/.deps"
    RESULT_VARIABLE EXTRACT_RESULT
  )
  if(NOT EXTRACT_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to extract Node.js headers archive")
  endif()
  
  # Rename extracted directory if needed
  if(NOT EXISTS "${LIBNODE_HEADERS_ROOT}" AND EXISTS "${CMAKE_SOURCE_DIR}/.deps/node-v${LibNode_VERSION}")
    file(RENAME "${CMAKE_SOURCE_DIR}/.deps/node-v${LibNode_VERSION}" "${LIBNODE_HEADERS_ROOT}")
  endif()
endif()

# ============================================================================
# Find include directory (in headers package)
# Point to root of include so headers are prefixed with node/
# ============================================================================
find_path(LibNode_INCLUDE_DIR
  NAMES node/node.h
  PATHS 
    "${LIBNODE_HEADERS_ROOT}/include"
    "${LIBNODE_HEADERS_ROOT}"
  NO_DEFAULT_PATH
)

# ============================================================================
# Find library (in binaries package)
# If .lib doesn't exist but DLL does, generate import lib from DLL (Windows)
# ============================================================================
find_library(LibNode_LIBRARY
  NAMES libnode node
  PATHS 
    "${LIBNODE_ROOT}/lib"
    "${LIBNODE_ROOT}"
  NO_DEFAULT_PATH
)

# Generate import library from DLL if not found (Windows MSVC)
if(WIN32 AND NOT LibNode_LIBRARY)
  set(_libnode_dll "${LIBNODE_ROOT}/libnode.dll")
  set(_libnode_lib "${LIBNODE_ROOT}/libnode.lib")
  set(_libnode_def "${LIBNODE_ROOT}/libnode.def")
  
  if(EXISTS "${_libnode_dll}" AND NOT EXISTS "${_libnode_lib}")
    message(STATUS "Generating import library from libnode.dll...")
    
    # Generate .def file from DLL exports
    find_program(GENDEF_EXE gendef)
    find_program(DLLTOOL_EXE dlltool)
    find_program(DUMPBIN_EXE dumpbin)
    find_program(LIB_EXE lib)
    
    if(DUMPBIN_EXE AND LIB_EXE)
      # MSVC toolchain: use dumpbin + lib
      message(STATUS "Using MSVC tools to generate import library")
      
      # Export symbols to .def
      execute_process(
        COMMAND "${DUMPBIN_EXE}" /EXPORTS "${_libnode_dll}"
        OUTPUT_FILE "${LIBNODE_ROOT}/exports.txt"
        RESULT_VARIABLE _dumpbin_result
      )
      
      if(_dumpbin_result EQUAL 0)
        # Parse exports and create .def file
        file(READ "${LIBNODE_ROOT}/exports.txt" _exports_content)
        file(WRITE "${_libnode_def}" "LIBRARY libnode\nEXPORTS\n")
        
        # Extract function names from dumpbin output
        string(REGEX MATCHALL "[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+([^\r\n]+)" _matches "${_exports_content}")
        foreach(_match ${_matches})
          string(REGEX REPLACE "^[0-9]+[ \t]+[0-9A-Fa-f]+[ \t]+[0-9A-Fa-f]+[ \t]+([^ \t\r\n]+).*$" "\\1" _symbol "${_match}")
          if(_symbol)
            file(APPEND "${_libnode_def}" "  ${_symbol}\n")
          endif()
        endforeach()
        
        # Generate import library
        execute_process(
          COMMAND "${LIB_EXE}" /DEF:${_libnode_def} /OUT:${_libnode_lib} /MACHINE:X64
          WORKING_DIRECTORY "${LIBNODE_ROOT}"
          RESULT_VARIABLE _lib_result
          ERROR_VARIABLE _lib_error
        )
        
        if(_lib_result EQUAL 0)
          message(STATUS "Successfully generated libnode.lib")
          set(LibNode_LIBRARY "${_libnode_lib}" CACHE FILEPATH "Path to libnode import library" FORCE)
        else()
          message(WARNING "Failed to generate import library with lib.exe")
        endif()
      endif()
    elseif(GENDEF_EXE AND DLLTOOL_EXE)
      # MinGW toolchain: use gendef + dlltool
      message(STATUS "Using MinGW tools to generate import library")
      execute_process(
        COMMAND "${GENDEF_EXE}" "${_libnode_dll}"
        WORKING_DIRECTORY "${LIBNODE_ROOT}"
      )
      execute_process(
        COMMAND "${DLLTOOL_EXE}" -d "${_libnode_def}" -l "${_libnode_lib}"
        WORKING_DIRECTORY "${LIBNODE_ROOT}"
      )
      if(EXISTS "${_libnode_lib}")
        set(LibNode_LIBRARY "${_libnode_lib}" CACHE FILEPATH "Path to libnode import library" FORCE)
      endif()
    else()
      message(WARNING "Cannot generate import library: need dumpbin+lib (MSVC) or gendef+dlltool (MinGW)")
    endif()
  endif()
endif()

# ============================================================================
# Find DLL and PDB (Windows only)
# ============================================================================
if(WIN32)
  find_file(LibNode_DLL
    NAMES ${LIBNODE_DLL_NAME}
    PATHS 
      "${LIBNODE_ROOT}/bin"
      "${LIBNODE_ROOT}"
    NO_DEFAULT_PATH
  )
  
  find_file(LibNode_PDB
    NAMES libnode.pdb
    PATHS 
      "${LIBNODE_ROOT}/bin"
      "${LIBNODE_ROOT}"
    NO_DEFAULT_PATH
  )
endif()

# ============================================================================
# Standard find package handling
# ============================================================================
find_package_handle_standard_args(LibNode
  REQUIRED_VARS LibNode_LIBRARY LibNode_INCLUDE_DIR
  VERSION_VAR LibNode_VERSION
)

if(LibNode_FOUND)
  set(LibNode_INCLUDE_DIRS ${LibNode_INCLUDE_DIR})
  set(LibNode_LIBRARIES ${LibNode_LIBRARY})
  
  message(STATUS "Found LibNode ${LibNode_VERSION}")
  message(STATUS "  Include: ${LibNode_INCLUDE_DIR}")
  message(STATUS "  Library: ${LibNode_LIBRARY}")
  if(WIN32 AND LibNode_DLL)
    message(STATUS "  DLL: ${LibNode_DLL}")
  endif()
  if(WIN32 AND LibNode_PDB)
    message(STATUS "  PDB: ${LibNode_PDB}")
  endif()
  
  # Create imported target
  if(NOT TARGET LibNode::LibNode)
    add_library(LibNode::LibNode SHARED IMPORTED)
    set_target_properties(LibNode::LibNode PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${LibNode_INCLUDE_DIR}"
    )
    if(WIN32 AND LibNode_DLL)
      set_target_properties(LibNode::LibNode PROPERTIES
        IMPORTED_IMPLIB "${LibNode_LIBRARY}"
        IMPORTED_LOCATION "${LibNode_DLL}"
      )
    else()
      set_target_properties(LibNode::LibNode PROPERTIES
        IMPORTED_LOCATION "${LibNode_LIBRARY}"
      )
    endif()
  endif()
endif()

mark_as_advanced(LibNode_INCLUDE_DIR LibNode_LIBRARY LibNode_DLL LibNode_PDB)
