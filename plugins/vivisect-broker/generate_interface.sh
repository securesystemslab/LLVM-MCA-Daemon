python3 -m grpc_tools.protoc -I. --python_out=. --pyi_out=. --grpc_python_out=. emulator.proto
protoc --grpc_out=. --cpp_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` emulator.proto
