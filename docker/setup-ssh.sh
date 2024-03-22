#!/bin/bash

set -e

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get install -y openssh-server
rm -rf /var/lib/apt/lists/*
service ssh start
