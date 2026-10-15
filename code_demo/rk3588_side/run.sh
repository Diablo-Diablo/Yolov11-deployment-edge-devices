#!/bin/sh
# 在 RK3588 板子上运行 YOLO11n 三核推理
# 使用方法：
#   chmod +x run.sh
#   ./run.sh                          # 相机模式，推理 200 帧
#   ./run.sh --benchmark              # Benchmark 模式（纯 NPU 吞吐）
#   ./run.sh --num_frames 100         # 自定义帧数

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# 让程序优先从本目录的 lib/ 找动态库（librknnrt.so）
export LD_LIBRARY_PATH="$SCRIPT_DIR/lib:$LD_LIBRARY_PATH"

# 屏蔽 RGA 内核日志（否则终端会被警告刷屏）
echo 3 > /proc/sys/kernel/printk 2>/dev/null

# 清理可能残留的旧进程
pkill -9 camera_yolo11_3core 2>/dev/null
sleep 1

# 确保结果目录存在
mkdir -p /userdata/results3

echo "启动 YOLO11n 三核并发推理..."
echo "结果图将保存到 /userdata/results3/"
echo ""

"$SCRIPT_DIR/camera_yolo11_3core" \
    --model_path "$SCRIPT_DIR/yolo11n_rk3588.rknn" \
    --num_frames 200 \
    --save_every 30 \
    "$@" 2>/dev/null
