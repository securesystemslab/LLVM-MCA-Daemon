python3 -m grpc_tools.protoc -I. --python_out=python_client --pyi_out=python_client --grpc_python_out=python_client emulator.proto
protoc --grpc_out=. --cpp_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` emulator.proto
