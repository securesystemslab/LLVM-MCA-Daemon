#!/bin/bash

echo "Running ./docker/setup-llvm.sh"
sudo ./docker/setup-llvm.sh
echo "Running ./docker/setup-deps.sh"
sudo ./docker/setup-deps.sh
echo "Running ./docker/setup-build.sh"
sudo ./docker/setup-build.sh
