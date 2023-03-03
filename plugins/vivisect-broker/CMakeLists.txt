cmake_minimum_required(VERSION 3.13)
project(example-grpc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

#
# Protobuf/Grpc source files
#
set(PROTO_FILES
    vivserver.proto
)

#
# Add Library target with protobuf sources
#
add_library(myproto ${PROTO_FILES})
target_link_libraries(myproto
    PUBLIC
        protobuf::libprotobuf
        gRPC::grpc
        gRPC::grpc++
)

#
# Compile protobuf and grpc files in myproto target to cpp
#
get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET myproto LANGUAGE cpp)
protobuf_generate(TARGET myproto LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")