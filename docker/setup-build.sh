#!/bin/bash

set -e

export DEBIAN_FRONTEND=noninteractive

if [ -z "${WORKSPACE_PATH}" ]; then
WORKSPACE_PATH=/work/LLVM-MCA-Daemon
fi

mkdir -p ${WORKSPACE_PATH}/build
cd ${WORKSPACE_PATH}/build

cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=/opt/LLVM-MCA-Daemon \
               -DCMAKE_C_COMPILER=clang-14 -DCMAKE_CXX_COMPILER=clang++-14 \
               -DLLVM_DIR=/opt/llvm-main/lib/cmake/llvm \
               -DLLVM_MCAD_ENABLE_PLUGINS="tracer;binja;vivisect" \
               ../
ninja
