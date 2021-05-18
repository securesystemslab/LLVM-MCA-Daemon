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