# Vivisect Broker

This broker works with emulators based off of the vivisect disassembler framework.

## Regenerating Protobuf/gRPC code

This directory already contains a copy of the protobuf/gRPC code for the
C++-based server and a python client. To update them, edit the interface
`emulator.proto` and rerun the following.

```
python3 -m grpc_tools.protoc -I. --python_out=. --pyi_out=. --grpc_python_out=. emulator.proto
protoc --grpc_out=. --cpp_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` emulator.proto
```

This may require running the following if they are not already installed on your system

```
sudo apt install -y protobuf-compiler python3-dev
pip install grpcio grpcio-tools protobuf
```

## Build

To build MCAD plugins you must set `-DLLVM_MCAD_ENABLE_PLUGINS=vivisect` when running
the CMake config step. For example

```
mkdir build && build
cmake -GNinja -DCMAKE_BUILD_TYPE=Debug \
              -DLLVM_DIR=/path/to/installed-llvm/lib/cmake/llvm \
              -DLLVM_MCAD_ENABLE_PLUGINS=vivisect \
              ..
ninja llvm-mcad MCADVivisectBroker
```

This broker also requires modifying the emulator to make use of the grpc client
to record instructions executed. This directory contains cm2350.patch which can
be applied to the CM2350 emulator

```
git clone git@github.com:Assured-Micropatching/CM2350-Emulator
cd CM2350-Emulator
git checkout 3960f630a74b738e43da7c3512c5ff09ea4df441
git apply /path/to/this/directory/cm2350.patch
cp -r /path/to/this/repo/plugins/vivisect-broker/grpc_client /path/to/CM2350-Emulator/cm2350/
# optionally add grpcio and protobuf to that repo's requirements.txt to install in the virtualenv
```

## Usage

This directory generates
`/$BUILD_DIR/plugins/vivisect-broker/libMCADVivisectBroker.so` which is the
broker plugin that is loaded by MCAD. It sets up a gRPC server in MCAD to handle
client requests from an emulator for recording instructions executed. See
`emulator.proto` for all the actions that an emulator can record in MCAD.

To use it, first start MCAD
```
./llvm-mcad -mtriple="powerpcle-linux-gnu" -mcpu="pwr10" \
            -load-broker-plugin=$PWD/plugins/vivisect-broker/libMCADVivisectBroker.so
```

Then start the emulator, for example in the case of the CM2350 emulator

```
./ECU.py -vvv test_program.bin
```

After shutting down the emulator, the gRPC client will ensure that the
connection to the server is closed and notify MCAD that the emulator has
terminated.
