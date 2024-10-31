#!/bin/bash

set -e

export DEBIAN_FRONTEND=noninteractive

if [ -z "${WORKSPACE_PATH}" ]; then
WORKSPACE_PATH=/work/LLVM-MCA-Daemon
fi

# Run ./setup-deps.sh before this to install dependencies needed to build LLVM

# Applying the patches requires that our git user has an identity.
git config --global user.email "workflow@example.com"
git config --global user.name "Workflow"

git clone https://github.com/llvm/llvm-project.git llvm
cd llvm
git am < ${WORKSPACE_PATH}/patches/add-identifier-to-mca-instruction.patch
git am < ${WORKSPACE_PATH}/patches/start-mapping-e500-itenerary-model-to-new-schedule.patch
git am < ${WORKSPACE_PATH}/patches/abstract-memory-group.patch
mkdir build && cd build
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=/opt/llvm-main \
               -DCMAKE_C_COMPILER=clang-14 -DCMAKE_CXX_COMPILER=clang++-14 \
               -DLLVM_USE_LINKER=lld-14 -DLLVM_ENABLE_ASSERTIONS=ON \
               -DLLVM_TOOL_LLVM_MCA_BUILD=ON \
               -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86;PowerPC" -DLLVM_DEFAULT_TARGET_TRIPLE=x86_64-pc-linux-gnu \
           ../llvm
ninja llvm-mca llvm-mc LLVMDebugInfoDWARF
ninja install
