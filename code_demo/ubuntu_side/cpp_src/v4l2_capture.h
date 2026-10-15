/*
 * v4l2_capture.h
 *
 * V4L2 multiplanar 摄像头采集封装（适用于 /dev/video22，rkisp NV12 输出）
 *
 * 使用示例:
 *   V4L2Capture cap;
 *   cap.open("/dev/video22", 3840, 2160, 4);
 *   cap.start();
 *   uint8_t* data; size_t size;
 *   cap.grab_frame(&data, &size);   // 阻塞等待下一帧
 *   // 使用 data[0..size-1]（NV12 原始数据）
 *   cap.release_frame();             // 归还 V4L2 buffer
 *   cap.stop();
 *   cap.close();
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#define V4L2_CAP_MAX_BUFS 8

struct V4L2Buffer {
    void*  start;   // mmap 地址
    size_t length;  // mmap 长度
};

class V4L2Capture {
public:
    V4L2Capture();
    ~V4L2Capture();

    // 打开设备，配置 NV12 multiplanar 格式，申请 num_bufs 个 mmap buffer
    int open(const char* device, int width, int height, int num_bufs = 4);

    // STREAMON，启动采集
    int start();

    // 阻塞等待下一帧（内部 select + DQBUF）
    // 返回后 *data 指向 mmap 地址，*size 为帧字节数（width*height*3/2）
    int grab_frame(uint8_t** data, size_t* size);

    // QBUF 归还当前帧，必须在下次 grab_frame 前调用
    int release_frame();

    // STREAMOFF
    int stop();

    // munmap + 关闭 fd
    void close();

    int width()  const { return width_;  }
    int height() const { return height_; }

private:
    int   fd_;
    int   width_;
    int   height_;
    int   num_bufs_;
    V4L2Buffer bufs_[V4L2_CAP_MAX_BUFS];
    int   cur_buf_idx_;   // 当前 DQBUF 得到的 buffer 下标
    bool  streaming_;
};
