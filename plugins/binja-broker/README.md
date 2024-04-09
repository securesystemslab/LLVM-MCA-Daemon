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

We use this information annotate the BN UI.  

## Regenerating Protobuf/gRPC code

This directory already contains a copy of the protobuf/gRPC code for the C++-based server and a python client. To **update** them, edit the interface `binja.proto` and rerun the following.  

```bash
$ # This is only necessary after updating binja.proto.
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

We assume that MCAD and `libMCADBinjaBroker.so` have already been built.  

To install the plugin, the easiest way is to create a symlink of the `binja-plugin` folder in the BinaryNinja plugins directory.  

```
ln -s binja-plugin ~/.binaryninja/plugins/mcad
```

On MacOS, you have to fully copy the conents:
```
cp -r binja-plugin "~/Library/Application Support/Binary Ninja"
```

Update variable `MCAD_BUILD_PATH` in `MCADBridge.py` to point to your build directory. For instance, `/home/chinmay_dd/Projects/LLVM-MCA-Daemon/build`. It will pick up `llvm-mcad` from there.

The Python interpreter used by Binary Ninja will have to have access to the PIP packages `grpcio` and `protobuf`.
The easiest way to set this up cleanly is with a separate Binary-Ninja Python virtual environment:

```
cd ~
python3 -m venv binja_venv
source binja_venv/bin/activate
pip3 install grpcio protobuf
```

Then, in the Binary Ninja settings, under Python, set
```
python.binaryOverride = /path/to/binja_venv/bin/python3
python.interpreter = /path/to/binja_venv/bin/python3
python.virtualenv = /path/to/binja_venv/lib/python3.12/site-packages
```

### Usage

1. Start the MCAD server in the backend - `Plugins/MCAD/1. Start Server`.
2. Navigate to any function in BinaryNinja.
3. To retrieve cycle counts for all basic blocks - `Plugins/MCAD/2. Get Cycle Counts for function`.
4. You can also choose to annotate specific basic blocks by adding Tag `Trace Member` to their first instruction.
   Then, `Plugins/MCAD/3. Get Cycle counts for annotated` will return cycle counts for only those functions.
5. Shut down server before closing BinaryNinja - `Plugins/MCAD/4. Stop Server`.

