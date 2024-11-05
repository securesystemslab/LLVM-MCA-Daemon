#!/bin/bash

# This script probably needs to run as sudo.

set -e

export DEBIAN_FRONTEND=noninteractive

dpkg --add-architecture i386

# General dependencies --
apt-get update
apt-get install -y \
       autoconf \
       binutils-dev \
       curl \
       g++ \
       g++-11-multilib \
       gcc \
       gcc-11-multilib \
       gfortran \
       gfortran-11-multilib \
       libboost-dev \
       libboost-regex-dev \
       libboost-serialization-dev \
       libboost-system-dev \
       libbsd-dev \
       libelf-dev \
       libelf-dev:i386 \
       libglib2.0-dev \
       libglib2.0-dev:i386 \
       libmemcached-dev \
       libpixman-1-dev \
       libpng-dev \
       libtool \
       mingw-w64 \
       nasm \
       pkg-config \
       python-is-python3 \
       python2 \
       python3-magic \
       python3-pip \
       python3-pyelftools \
       tar \
       unzip \
       wget \
       zip \
       zstd \
       `# And some utilities` \
       bash-completion \
       byobu \
       ccache \
       htop \
       vim \
       nano

# Dependencies to build LLVM --
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

rm -rf /root/.cache
