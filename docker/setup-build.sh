#!/bin/bash

set -e

export DEBIAN_FRONTEND=noninteractive

mkdir LLVM-MCA-Daemon/build
cd LLVM-MCA-Daemon/build

cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/opt/LLVM-MCA-Daemon \
               -DCMAKE_C_COMPILER=clang-14 -DCMAKE_CXX_COMPILER=clang++-14 \
               -DLLVM_DIR=/opt/llvm-main/lib/cmake/llvm \
               -DLLVM_MCAD_ENABLE_PLUGINS="tracer;binja;vivisect" \
               ../
ninja
