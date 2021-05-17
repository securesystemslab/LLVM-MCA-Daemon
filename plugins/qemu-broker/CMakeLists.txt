if (WIN32)
  message(FATAL_ERROR "This plugin does not support Windows")
endif()

# Search for the Flatbuffers header
find_path(_FLATBUFFERS_HEADER flatbuffers.h
  PATH_SUFFIXES flatbuffers
  REQUIRED)
if (_FLATBUFFERS_HEADER STREQUAL "_FLATBUFFERS_HEADER-NOTFOUND")
  message(FATAL_ERROR "This plugin requires Flatbuffers..."
                      "because I'm too lazy to do serialization by myself ;-)")
endif()
message(STATUS "Using Flatbuffers at ${_FLATBUFFERS_HEADER}")

# If there is Flatbuffers compiler, re-generate the header
find_program(_FLATBUFFERS_COMPILER flatc)

set(_FBS_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/Serialization)
set(_FBS_HEADER_PATH ${_FBS_OUTPUT_DIR}/mcad_generated.h)
if (NOT _FLATBUFFERS_COMPILER STREQUAL "_FLATBUFFERS_COMPILER-NOTFOUND")
  message(STATUS "Found Flatbuffers compiler at ${_FLATBUFFERS_COMPILER}")
  set(_FBS_INPUT ${CMAKE_CURRENT_SOURCE_DIR}/Serialization/mcad.fbs)
  add_custom_command(OUTPUT ${_FBS_HEADER_PATH}
    COMMAND ${_FLATBUFFERS_COMPILER}
    ARGS --cpp -o ${_FBS_OUTPUT_DIR} ${_FBS_INPUT}
    DEPENDS ${_FBS_INPUT})
else()
  message(STATUS "Flatbuffers compiler not found, will not re-generate the header")
endif()
add_custom_target(generate-fbs
  DEPENDS ${_FBS_HEADER_PATH})

add_llvm_library(MCADQemuBroker SHARED
  Broker.cpp

  # Components like Support, MC, TargetDesc or TargetInfo
  # should be already available in llvm-mcad
  LINK_COMPONENTS
  AllTargetsDisassemblers
  )
add_dependencies(MCADQemuBroker generate-fbs)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

add_subdirectory(Qemu)

add_subdirectory(utils)
