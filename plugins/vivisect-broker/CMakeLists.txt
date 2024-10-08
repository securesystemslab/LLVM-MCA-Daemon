set(CMAKE_CXX_STANDARD 17)

add_library(vivisect_service_proto emulator.proto)
target_compile_options(vivisect_service_proto PRIVATE "-fPIC")
target_link_libraries(vivisect_service_proto
    PUBLIC
        grpc++_reflection
        grpc++
        libprotobuf
)

# Compile protobuf and grpc files in vivisect_service_proto target to cpp
protobuf_generate(TARGET vivisect_service_proto LANGUAGE cpp OUT_VAR proto_files)
protobuf_generate(TARGET vivisect_service_proto LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")

target_include_directories(vivisect_service_proto PUBLIC ${proto_include_dir})

set(LLVM_HAVE_LINK_VERSION_SCRIPT 1)
find_package (Python3 REQUIRED
  COMPONENTS Interpreter Development)

# FIXME: We need to export llvm::Any::TypeId<T>::Id as (weak) global symbol
# or the id for each type will not be unique and break the whole llvm::Any
# system. However, since llvm's symbol exporting script processor doesn't
# do symbol name mangling for us, the current Broker.exports script probably
# only works on Linux (and maybe MacOSX?).
set(LLVM_EXPORTED_SYMBOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/Broker.exports)

add_llvm_library(MCADVivisectBroker SHARED
  Broker.cpp
  
  # Components like Support, MC, TargetDesc or TargetInfo
  # should be already available in llvm-mcad
  LINK_COMPONENTS
  DebugInfoDWARF
  Object
  )
add_dependencies(MCADVivisectBroker 
    vivisect_service_proto)

# MacOS-specific fix:
# The mcadGetBrokerPluginInfo() function, which is defined by the individual
# plugins, calls into functions defined in the main llvm-mcad executable into
# which the plugin will be loaded at runtime. We cannot link against the main
# executable; instead those calls should be resolved at runtime. We achieve this
# on Linux using attribute(weak) in the source code; the MacOS linker requires
# -undefied dynamic_lookup as a command line argument.
if(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  target_link_options(MCADVivisectBroker PUBLIC -Wl,-undefined -Wl,dynamic_lookup)
endif()

target_link_libraries(MCADVivisectBroker PUBLIC vivisect_service_proto)

unset(LLVM_EXPORTED_SYMBOL_FILE)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
