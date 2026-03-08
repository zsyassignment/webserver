#!/bin/bash

# 设置遇到错误立即退出
set -e

echo "📦 正在检查并安装依赖包..."

sudo apt-get update
sudo apt-get install -y build-essential cmake libcurl4-openssl-dev

echo "mkdir build..."
cd "$(dirname "$0")"
if [ -d "build" ]; then
    rm -rf build/*
else
    mkdir build
fi
cd build

echo "cmaking ..."
cmake ..

# -j$(nproc) 自动获取你 CPU 的核心数
make -j$(nproc)