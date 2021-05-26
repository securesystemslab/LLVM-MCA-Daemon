# Broker for QEMU
A Broker that works with QEMU TCG plugin.
## Preparations
Since we add some custom features in QEMU, please apply the patch `patches/qemu-patch.diff` in the following way:
```bash
cd qemu
# The patch is built on top of this HEAD
git checkout 0cef06d18762374c94eb4d511717a4735d668a24
# Apply the patch
git apply /path/to/patches/qemu-patch.diff
```
After applying the patch, please build QEMU in the following way:
```bash
mkdir -p build && cd build
../configure --target-list="arm-linux-user" \
             --enable-capstone \
             --enable-debug \
             --enable-plugins
ninja qemu-arm
```
In addition to QEMU. This Broker also uses [Flatbuffers](https://google.github.io/flatbuffers) for serialization. You can install it on Ubuntu via the following command:
```bash
sudo apt install libflatbuffers-dev
```
The [Flatbuffers compiler](https://google.github.io/flatbuffers/flatbuffers_guide_using_schema_compiler.html)(i.e. `flatc`) is an optional dependency. With `flatc` you can re-generate the Flatbuffers header (i.e. `Serialization/mcad_generated.h`). Here is the command to install it on Ubuntu:
```bash
sudo apt install flatbuffers-compiler
```

## Build
Things in this folder -- including the broker plugin and the QEMU "relay" plugin -- will be built with rest of the LLVM-MCAD project if the `-DLLVM_MCAD_BUILD_PLUGINS=ON` is given when configuring.

In addition, you need to supply the QEMU include folder via the `QEMU_INCLUDE_DIR` CMake argument. Here is a complete example:
```bash
mkdir .build && cd .build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
               -DLLVM_DIR=/path/to/installed-llvm/lib/cmake/llvm \
               -DLLVM_MCAD_BUILD_PLUGINS=ON \
               -DQEMU_INCLUDE_DIR=/path/to/qemu/include \
               ../
ninja all
```
If you want to use a custom Flatbuffers, you can use the `CMAKE_PREFIX_PATH` CMake variable to control the search path. For example:
```bash
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
               -DLLVM_DIR=/path/to/installed-llvm/lib/cmake/llvm \
               -DLLVM_MCAD_BUILD_PLUGINS=ON \
               -DQEMU_INCLUDE_DIR=/path/to/qemu/include \
               -DCMAKE_PREFIX_PATH=/path/to/your/flatbuffers \
               ../
```

## Usage
This subproject will generate two artifacts:
 1. `/<build dir>/plugins/qemu-broker/libMCADQemuBroker.so` is the Broker plugin that will be loaded into the `llvm-mcad` tool.
 2. `/<build dir>/plugins/qemu-broker/Qemu/libQemuRelay.so` is the plugin that will be loaded into QEMU.

These two plugins communicate with each other via a TCP socket. More specifically, `libMCADQemuBroker.so` will act as a server which accepts ingress connection from `libQemuRelay.so`. The `libQemuRelay.so` plugin then transmits translation blocks and execution traces collected from QEMU instrumentations before translated by `libMCADQemuBroker` into a stream of `MCInst` instances that will be consumed by `llvm-mcad`.

Here is an example of launching the server side:
```bash
# Server
cd .build
./llvm-mcad -mtriple="armv7-linux-gnueabihf" -mcpu="cortex-a57" \
            -load-broker-plugin=$PWD/plugins/qemu-broker/libMCADQemuBroker.so \
            -broker-plugin-arg-host="localhost:9487"
```
As you can see in the above snippet, we're using `-load-broker-plugin=<plugin path>` to load and use the qemu-broker plugin. The `-broker-plugin-arg-host` is an additional plugin argument which we will talk about it shortly.

On the client side:
```bash
# Client
/path/to/qemu/build/qemu-arm -L /usr/arm-linux-gnueabihf \
      -plugin /path/to/llvm-mcad/.build/plugins/qemu-broker/Qemu/libQemuRelay.so,\
      arg="-addr=127.0.0.1",arg="-port=9487" \
      -d plugin ./hello_world
```
Here, we're using `-plugin <path to plugin>` to load our qemu relay plugin. Additional arguments for this plugin, which we will also introduce shortly, are passed via `arg=...` concatenated right after the plugin path.

### Additional plugin arguments
For the `libMCADQemuBroker.so` plugin, here is a list of supported arguments:
 - `-host=<address>:<port>`. The address and port to listen on.
 - `-max-accepted-connection=<number>`. By default, this plugin will exit after finishing a single connection. You can use this option to adjust the number of connections before exiting, or -1 to waive the limit.
 - `-binary-regions=<manifest file>`. See the [_Binary Regions_](#binary-regions) section below.

To use any of the above argument, please prefix them with `-broker-plugin-arg` before passing to `llvm-mcad`. For example:
```bash
./llvm-mcad ... -broker-plugin-arg-max-accepted-connection=87 ...
```

For the `libQemuRelay.so` plugin, here is a list of supported arguments:
 - `-addr=<server address>`. Address to the server. Note that we currently don't support name address like `localhost` or domain name, please use IP address here.
 - `-port=<server port>`. Port to the server.
 - `-only-main-code`. Only send instructions that are belong to the main executable. This flag can get rid of unrelated execution traces, like those generated from interpreter (i.e. `ld.so`). But this might also get rid of shared library loaded during run-time.

To use any of the above argument, please pass them via `-arg="..."`. For example:
```bash
qemu-arm ... -plugin <plugin path>,arg="-addr=127.0.0.1",arg="-port=87" ...
```
Note that since we're using LLVM's command line infrastructure for this specific plugin (but not `libMCADQemuBroker.so`), you can use the following way to see all available command line options:
```bash
qemu-arm ... -plugin <plugin path>,arg="-help" ...
```

## Binary regions
_TBA_