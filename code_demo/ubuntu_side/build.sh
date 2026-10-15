#!/bin/bash
# 交叉编译 camera_yolo11_3core，产物自动安装到 ../rk3588_side/
# 运行方式：chmod +x build.sh && ./build.sh
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$SCRIPT_DIR/cpp_src"
BUILD_DIR="$SCRIPT_DIR/build"
INSTALL_DIR="$SCRIPT_DIR/../rk3588_side"

# 检查交叉编译器
if ! command -v aarch64-linux-gnu-g++ &>/dev/null; then
    echo "错误：未找到交叉编译器，请先安装："
    echo "  sudo apt update && sudo apt install -y gcc-aarch64-linux-gnu g++-aarch64-linux-gnu"
    exit 1
fi

echo "=== 交叉编译 camera_yolo11_3core (aarch64) ==="
echo "源码目录: $SRC_DIR"
echo "产物目录: $INSTALL_DIR"
echo ""

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

cmake "$SRC_DIR" \
    -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc \
    -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DTARGET_SOC=rk3588 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_DIR"

make -j$(nproc)
cmake --install .

echo ""
echo "=== 编译完成！==="
echo "已安装到 rk3588_side/ ："
ls -lh "$INSTALL_DIR/camera_yolo11_3core" 2>/dev/null || true
ls -lh "$INSTALL_DIR/lib/" 2>/dev/null || true
echo ""
echo "下一步：将整个 rk3588_side/ 文件夹传到板子上"
echo "  scp -r ../rk3588_side root@<板子IP>:/userdata/"
