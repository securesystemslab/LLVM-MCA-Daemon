#!/bin/bash

set -e

export DEBIAN_FRONTEND=noninteractive

dpkg --add-architecture i386

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
rm -rf /var/lib/apt/lists/*

rm -rf /root/.cache
