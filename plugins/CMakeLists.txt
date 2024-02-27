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

# add_subdirectory(qemu-broker)
add_subdirectory(tracer-broker)
add_subdirectory(vivisect-broker)
add_subdirectory(binja-broker)
