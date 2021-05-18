# LLVM MCA Daemon
[LLVM MCA](https://llvm.org/docs/CommandGuide/llvm-mca.html) that performs analysis online.

## Preparations
This project uses a special version of LLVM. Please use the following way to clone it:
```bash
git clone -b dev-incremental-mca git@github.com:securesystemslab/multiarch-llvm-project.git
```
To build it, here are some suggested CMake configurations:
```bash
cd multiarch-llvm-project
mkdir .build && cd .build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
               -DBUILD_SHARED_LIBS=ON \
               -DLLVM_TARGETS_TO_BUILD="X86;ARM" \
               -DLLVM_USE_LINKER=gold \
               ../llvm
ninja llvm-mca
```
Note that LLVM-MCAD uses a modular design, so components related to QEMU are no longer built by default. Please checkout their prerequisites inside their folders under the `plugins` directory.

## Build
You only need one additional CMake argument: `LLVM_DIR`. It points to LLVM's CMake folder. Here is an example:
```bash
mkdir .build && cd .build
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug \
               -DLLVM_DIR=/path/to/installed-llvm/lib/cmake/llvm \
               ../
ninja all
```
For instance, if you build `multiarch-llvm-project` using the previous steps, _"/path/to/installed-llvm/lib/cmake/llvm"_ will be _"/path/to/multiarch-llvm-project/.build/lib/cmake/llvm"_.

Note that plugins under the `plugins` folder are not built by default. Please add `-DLLVM_MCAD_BUILD_PLUGINS=ON` if you want to build them. Here are some other CMake arguments you can tweak:
 - `LLVM_MCAD_ENABLE_ASAN`. Enable the address sanitizer.
 - `LLVM_MCAD_ENABLE_TCMALLOC`. Uses tcmalloc and its heap profiler.
 - `LLVM_MCAD_ENABLE_PROFILER`. Uses CPU profiler from [gperftools](https://github.com/gperftools/gperftools).
 - `LLVM_MCAD_FORCE_ENABLE_STATS`. Enable LLVM statistics even in non-debug builds.

## Usages
Here is an example of using `llvm-mcad` -- the main command line tool -- with the qemu-broker Broker plugin.

First, on the server side:
```bash
# Server
cd .build
./llvm-mcad -mtriple="armv7-linux-gnueabihf" -mcpu="cortex-a57" \
            --load-broker-plugin=$PWD/plugins/qemu-broker/libMCADQemuBroker.so \
            -broker-plugin-arg-host="localhost:9487"
```
Then, on the client side:
```bash
# Client
/path/to/qemu/build/qemu-arm -L /usr/arm-linux-gnueabihf \
      -plugin /path/to/llvm-mcad/.build/plugins/qemu-broker/Qemu/libQemuRelay.so,\
      arg="-addr=127.0.0.1",arg="-port=9487" \
      -d plugin ./hello_world
```