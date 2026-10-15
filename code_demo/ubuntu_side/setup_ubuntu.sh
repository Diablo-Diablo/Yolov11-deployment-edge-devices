#!/bin/bash
# 在 Ubuntu 上一键安装所有编译依赖
# 运行方式：chmod +x setup_ubuntu.sh && ./setup_ubuntu.sh
set -e

echo "=== 安装交叉编译工具链和构建工具 ==="
sudo apt update
sudo apt install -y \
    gcc-aarch64-linux-gnu \
    g++-aarch64-linux-gnu \
    cmake \
    make

echo ""
echo "=== 验证安装结果 ==="
echo -n "cmake    : "; cmake --version | head -1
echo -n "make     : "; make --version | head -1
echo -n "gcc 交叉 : "; aarch64-linux-gnu-gcc --version | head -1
echo -n "g++ 交叉 : "; aarch64-linux-gnu-g++ --version | head -1

echo ""
echo "=== 所有依赖安装完毕，可以运行 ./build.sh 编译了 ==="
