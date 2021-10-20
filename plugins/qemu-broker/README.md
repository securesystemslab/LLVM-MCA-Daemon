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
../configure --target-list="aarch64-linux-user,arm-linux-user,x86_64-linux-user" \
             --enable-capstone \
             --enable-debug \
             --enable-plugins
ninja qemu-arm qemu-x86_64 qemu-aarch64
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
# ARM
./llvm-mcad -mtriple="armv7-linux-gnueabihf" -mcpu="cortex-a57" \
            -load-broker-plugin=/path/to/libMCADQemuBroker.so \
            -broker-plugin-arg-host="localhost:9487"
# X86
./llvm-mcad -mtriple="x86_64-unknown-linux-gnu" -mcpu="skylake" \
            --load-broker-plugin=$PWD/plugins/qemu-broker/libMCADQemuBroker.so \
            -broker-plugin-arg-host="localhost:9487"
```
As you can see in the above snippet, we're using `-load-broker-plugin=<plugin path>` to load and use the qemu-broker plugin. The `-broker-plugin-arg-host` is an additional plugin argument which we will talk about it shortly.

On the client side:
```bash
# Client
# ARM
/path/to/qemu/build/qemu-arm -L /usr/arm-linux-gnueabihf \
      -plugin /path/to/libQemuRelay.so,\
      arg="-addr=127.0.0.1",arg="-port=9487" \
      -d plugin ./hello_world.arm
# X86
/path/to/qemu/build/qemu-x86_64 \
      -plugin /path/to/llvm-mcad/.build/plugins/qemu-broker/Qemu/libQemuRelay.so,\
      arg="-addr=127.0.0.1",arg="-port=9487" \
      -d plugin ./hello_world.x86_64
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
By using the binary regions feature, you are able to analyze only part of the execution trace. Users can specify their chosen regions using a manifest JSON file and supply it via the `-binary-regions` Broker argument. For example:
```bash
./llvm-mcad ... -load-broker-plugin=/path/to/libQemuBroker.so \
                -broker-plugin-arg-binary-regions=my_manifest.json ...
```
We currently support two different manifest formats: Symbol-based and address-based.

### Symbol-based format
This format allows you to analyze traces from a specific function and its subsequent callee functions (All callees dominated by this function in the call graph). Here is the JSON format:
```json
{
      "file": "/path/to/target_executable",
      "regions": [
            {
                  "symbol": "Name of the symbol to instrument",
                  "description": "Custom textual description for this region (optional)",
                  "offsets": [0, 0] /*Offsets to be applied at the beginning / end of the region (optional)*/
            }
      ]
}
```
For example:
```json
{
      "file": "/path/to/hello_world",
      "regions": [
            {
                  "symbol": "main"
            },
            {
                  "symbol": "foo",
                  "description": "The fooooo function",
                  "offsets": [4, -8]
            }
      ]
}
```
The manifest above instruments two regions. The first region starts the instrumentation after the first instruction in `main` is executed, and ends it when the last one is executed. The second region starts the instrumentation after the instruction at `foo` + 4 bytes is executed and ends after hitting instruction at `foo` + \<size of `foo`\> - 8 bytes.

Note that this manifest formats has the following drawbacks:
 1. Does not support MachO since it doesn't provide the size of a function symbol.
 2. It's nearly impossible to specify the starting address of the _last_ instruction in a function without the help of end offset compensation (In other words, the first region in the example above is actually pretty inaccurate).

### Address-based format
This format is basically doing the same thing as symbol-based format, except that it gives you direct control on the addresses to trigger / terminate the instrumentation. Here is the format:
```json
[
      {
            "start": 0, /*Can be number of non-decimal number wrapped in a string*/
            "end": 0,
            "description": "Optional textual description"
      }
]
```
The `start` and `end` field is the offset to the instruction that starts and ends the instrumentation, respectively.


Currently neither of these formats support overlapped regions (including recursive function calls). Which is a limitation imposed by MCA.