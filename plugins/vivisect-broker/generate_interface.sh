if [ -z "$grpc_cpp_plugin_path" ]; then
    grpc_cpp_plugin_path=$(which grpc_cpp_plugin)
    if [ -z "$grpc_cpp_plugin_path" ]; then
        grpc_cpp_plugin_path=$(realpath ../../build/_deps/grpc-build/grpc_cpp_plugin)
        if [ -z "$grpc_cpp_plugin_path" ]; then
            echo "Did not find grpc_cpp_plugin. You should be able to get one by configuring CMake, which will clone gRPC into build_dir/_deps/grpc-build; then ninja grpc_cpp_plugin";
        fi
    fi
fi
python3 -m grpc_tools.protoc -I. --python_out=grpc_client --pyi_out=grpc_client --grpc_python_out=grpc_client emulator.proto
protoc --grpc_out=. --cpp_out=. "--plugin=protoc-gen-grpc=$grpc_cpp_plugin_path" emulator.proto --experimental_allow_proto3_optional
