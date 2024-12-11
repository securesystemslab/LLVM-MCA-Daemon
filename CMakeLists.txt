cmake_minimum_required(VERSION 3.13)
project(llvm-mcad)

set(CMAKE_CXX_STANDARD 17)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION}")

add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIR})

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

# Handle RTTI stuff, which often leads to error
if(NOT ${LLVM_ENABLE_RTTI})
  if("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang" OR
      "${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU" OR
      "${CMAKE_CXX_COMPILER_ID}" STREQUAL "Intel")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
    message(STATUS "Disable RTTI")
  elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /GR-")
    message(STATUS "Disable RTTI")
  endif()
  # Do not give any flags for other less widely used
  # compilers
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

option(LLVM_MCAD_ENABLE_TCMALLOC "Enable tcmalloc in gpertools for memory profiling" OFF)

option(LLVM_MCAD_ENABLE_PROFILER "Enable CPU profiler in gpertools" OFF)

# See plugins/CMakeLists.txt for LLVM_MCAD_ENABLE_PLUGINS option

# Sanitizers
option(LLVM_MCAD_ENABLE_ASAN "Enable address sanitizer" OFF)

if ("${CMAKE_CXX_COMPILER_ID}" MATCHES "Clang")
  # Clang ASAN currently has some problem using with TCMalloc
  if (LLVM_MCAD_ENABLE_TCMALLOC AND LLVM_MCAD_ENABLE_ASAN)
    message(FATAL_ERROR "TCMalloc can not be used with Clang's ASAN")
  endif()
endif()

if (LLVM_MCAD_ENABLE_ASAN)
  message(STATUS "Address sanitizer is enabled")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
endif()

set(_EXTRA_LIBS )

if (LLVM_MCAD_ENABLE_TCMALLOC OR LLVM_MCAD_ENABLE_PROFILER)
  set(_GPERF_COMPONENTS )
  if (LLVM_MCAD_ENABLE_TCMALLOC)
    list(APPEND _GPERF_COMPONENTS "tcmalloc")
    add_compile_definitions(LLVM_MCAD_ENABLE_TCMALLOC)
  endif ()
  if (LLVM_MCAD_ENABLE_PROFILER)
    list(APPEND _GPERF_COMPONENTS "profiler")
    add_compile_definitions(LLVM_MCAD_ENABLE_PROFILER)
  endif ()

  find_package(Gperftools REQUIRED
    COMPONENTS ${_GPERF_COMPONENTS})

  include_directories(${GPERFTOOLS_INCLUDE_DIRS})

  list(APPEND _EXTRA_LIBS ${GPERFTOOLS_LIBRARIES})
endif()

include_directories(${CMAKE_CURRENT_SOURCE_DIR})

set(LLVM_LINK_COMPONENTS
    AllTargetsAsmParsers
    AllTargetsMCAs
    AllTargetsDescs
    AllTargetsDisassemblers
    AllTargetsInfos
    MC
    MCA
    MCParser
    Support
    TargetParser
    )

set(_MCAVIEWS_SOURCE_FILES
    MCAViews/InstructionView.cpp
    MCAViews/SummaryView.cpp
    MCAViews/TimelineView.cpp
    )

set(_CUSTOMHW_SOURCE_FILES
    CustomHWUnits/CustomSourceMgr.cpp
    CustomHWUnits/MCADLSUnit.cpp
    CustomHWUnits/NaiveBranchPredictorUnit.cpp
    CustomHWUnits/BranchUnit.cpp
   )

set(_CUSTOM_STAGES_SOURCE_FILES
    CustomStages/MCADFetchDelayStage.cpp
   )

set(_BROKERS_SOURCE_FILES
    Brokers/BrokerPlugin.cpp
    Brokers/RawBytesBroker.cpp
    Brokers/AsmFileBroker.cpp
    Brokers/AsmUtils/CodeRegion.cpp
    Brokers/AsmUtils/CodeRegionGenerator.cpp
    )

set(_SOURCE_FILES
    llvm-mcad.cpp
    ${_MCAVIEWS_SOURCE_FILES}
    ${_CUSTOMHW_SOURCE_FILES}
    ${_CUSTOM_STAGES_SOURCE_FILES}
    ${_BROKERS_SOURCE_FILES}
    MCAWorker.cpp
    MetadataCategories.cpp
    PipelinePrinter.cpp
    )

add_llvm_executable(llvm-mcad
  ${_SOURCE_FILES}

  ${SUPPORT_PLUGINS}
  )
export_executable_symbols(llvm-mcad)

target_link_libraries(llvm-mcad
  PRIVATE ${_EXTRA_LIBS}
  )

unset(LLVM_LINK_COMPONENTS)

add_subdirectory(plugins)
