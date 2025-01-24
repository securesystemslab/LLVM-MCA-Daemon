python3 -m grpc_tools.protoc -I. --python_out=grpc_client --pyi_out=grpc_client --grpc_python_out=grpc_client emulator.proto
protoc --grpc_out=. --cpp_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` emulator.proto --experimental_allow_proto3_optional
