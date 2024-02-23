# Binja Broker

This broker provides integration of MCAD with BinaryNinja. BN is expected to send a `trace` of instructions and MCAD will provide detailed cycle count information.  

```
 |------|    Instructions     |----------|
 |      | ------------------> |          |
 |  BN  |   (grpc channel)    |   MCAD   |
 |      | <------------------ |          |
 |------|    Cycle Counts     |----------|
```

MCAD provides information about when each instruction started executing, finished executing and whether there was pipeline pressure during its dispatch.  

We use this information annotate the BN UI. This is still WIP.  

## Regenerating Protobuf/gRPC code

This directory already contains a copy of the protobuf/gRPC code for the C++-based server and a python client. To update them, edit the interface `binja.proto` and rerun the following.  

```bash
$ protoc --grpc_out=. --cpp_out=. --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` binja.proto
$ cd binja-plugin
$ python3 -m grpc_tools.protoc -I. --python_out=. --pyi_out=. --grpc_python_out=. ../binja.proto
```

This may require running the following if they are not already installed on your system -  

```
pip install grpcio
pip install protobuf
```

## Build

To build MCAD plugins you must set `-DLLVM_MCAD_BUILD_PLUGINS=ON` when running the CMake config step.  

To install the plugin, the easiest way is to create a symlink of the `binja-plugin` folder in the `plugins/binja-broker` directory.  

```
ln -s binja-plugin ~/.binaryninja/plugins/mcad
```

### Usage

WIP
