set(LLVM_MCAD_ALL_PLUGINS "qemu;tracer;vivisect;binja")
set(LLVM_MCAD_ENABLE_PLUGINS "" CACHE STRING
        "Semicolon-separated list of MCAD plugins to build (${LLVM_KNOWN_PROJECTS}), or \"all\". Empty string (default) builds none.")
if (LLVM_MCAD_ENABLE_PLUGINS STREQUAL "all")
        set( LLVM_MCAD_ENABLE_PLUGINS ${LLVM_MCAD_ALL_PLUGINS})
endif()

if (NOT LLVM_MCAD_ENABLE_PLUGINS STREQUAL "")

	include(FetchContent)

	set(ABSL_ENABLE_INSTALL ON)
	SET(ABSL_PROPAGATE_CXX_STD ON)
	SET(ABSL_BUILD_TESTING OFF)

	FetchContent_Declare(
	  gRPC
	  GIT_REPOSITORY https://github.com/grpc/grpc
	  GIT_TAG        v1.60.0 
	)
	set(FETCHCONTENT_QUIET OFF)
	FetchContent_MakeAvailable(gRPC)

	include(${grpc_BINARY_DIR}/third_party/protobuf/cmake/protobuf/protobuf-generate.cmake)

	set(grpc_cpp_plugin_location $<TARGET_FILE:grpc_cpp_plugin>)

	set(proto_include_dir "${CMAKE_CURRENT_BINARY_DIR}")
	message(STATUS "Protobuf include directory: ${proto_include_dir}")

	if ("qemu" IN_LIST LLVM_MCAD_ENABLE_PLUGINS)
		add_subdirectory(qemu-broker)
	endif ()
	if ("tracer" IN_LIST LLVM_MCAD_ENABLE_PLUGINS)
		add_subdirectory(tracer-broker)
	endif ()
	if ("vivisect" IN_LIST LLVM_MCAD_ENABLE_PLUGINS)
		add_subdirectory(vivisect-broker)
	endif ()
	if ("binja" IN_LIST LLVM_MCAD_ENABLE_PLUGINS)
		add_subdirectory(binja-broker)
	endif ()

endif()
