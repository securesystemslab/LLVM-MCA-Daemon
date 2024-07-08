#!/bin/bash

set -e

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y software-properties-common wget

wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc
add-apt-repository "deb http://apt.llvm.org/focal/ llvm-toolchain-focal-14 main"

apt-get update
apt-get install -y \
       build-essential \
       clang-14 \
       cmake \
       lld-14 \
       git \
       ninja-build
rm -rf /var/lib/apt/lists/*

git clone https://github.com/llvm/llvm-project.git llvm
mkdir llvm/build
cd llvm/build
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/opt/llvm-main \
               -DCMAKE_C_COMPILER=clang-14 -DCMAKE_CXX_COMPILER=clang++-14 \
               -DLLVM_USE_LINKER=lld-14 -DLLVM_ENABLE_ASSERTIONS=ON \
               -DLLVM_TOOL_LLVM_MCA_BUILD=ON \
               -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;PowerPC" -DLLVM_DEFAULT_TARGET_TRIPLE=x86_64-pc-linux-gnu \
           ../llvm
ninja llvm-mca llvm-mc LLVMDebugInfoDWARF
ninja install
