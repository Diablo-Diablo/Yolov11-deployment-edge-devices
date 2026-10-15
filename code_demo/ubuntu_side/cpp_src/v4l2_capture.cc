/*
 * v4l2_capture.cc
 *
 * V4L2 multiplanar 摄像头采集实现
 * 适用于 /dev/video22（rkisp，Video Capture Multiplanar，NV12，3840×2160）
 */

#include "v4l2_capture.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <linux/videodev2.h>

static int xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do { r = ioctl(fd, request, arg); } while (r == -1 && errno == EINTR);
    return r;
}

V4L2Capture::V4L2Capture()
    : fd_(-1), width_(0), height_(0), num_bufs_(0),
      cur_buf_idx_(-1), streaming_(false)
{
    memset(bufs_, 0, sizeof(bufs_));
}

V4L2Capture::~V4L2Capture() {
    if (streaming_) stop();
    close();
}

int V4L2Capture::open(const char* device, int width, int height, int num_bufs) {
    fd_ = ::open(device, O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        printf("ERROR: open %s failed: %s\n", device, strerror(errno));
        return -1;
    }

    // 确认是 Video Capture Multiplanar 设备
    struct v4l2_capability cap;
    if (xioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        printf("ERROR: VIDIOC_QUERYCAP failed\n");
        ::close(fd_); fd_ = -1; return -1;
    }
    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        printf("ERROR: %s is not a Video Capture Multiplanar device\n", device);
        ::close(fd_); fd_ = -1; return -1;
    }

    // 设置格式：NV12，width×height
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmt.fmt.pix_mp.width       = width;
    fmt.fmt.pix_mp.height      = height;
    fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12;
    fmt.fmt.pix_mp.field       = V4L2_FIELD_NONE;
    fmt.fmt.pix_mp.num_planes  = 1;
    if (xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        printf("ERROR: VIDIOC_S_FMT failed: %s\n", strerror(errno));
        ::close(fd_); fd_ = -1; return -1;
    }
    printf("V4L2 format set: %dx%d pixelformat=0x%x num_planes=%d\n",
           fmt.fmt.pix_mp.width, fmt.fmt.pix_mp.height,
           fmt.fmt.pix_mp.pixelformat, fmt.fmt.pix_mp.num_planes);

    // 申请 buffer
    if (num_bufs > V4L2_CAP_MAX_BUFS) num_bufs = V4L2_CAP_MAX_BUFS;
    struct v4l2_requestbuffers reqbuf;
    memset(&reqbuf, 0, sizeof(reqbuf));
    reqbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    reqbuf.count  = num_bufs;
    if (xioctl(fd_, VIDIOC_REQBUFS, &reqbuf) < 0) {
        printf("ERROR: VIDIOC_REQBUFS failed: %s\n", strerror(errno));
        ::close(fd_); fd_ = -1; return -1;
    }
    num_bufs_ = reqbuf.count;
    printf("V4L2 allocated %d buffers\n", num_bufs_);

    // mmap 所有 buffer
    for (int i = 0; i < num_bufs_; i++) {
        struct v4l2_plane planes[1];
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.index    = i;
        buf.m.planes = planes;
        buf.length   = 1;  // num_planes
        if (xioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            printf("ERROR: VIDIOC_QUERYBUF[%d] failed: %s\n", i, strerror(errno));
            ::close(fd_); fd_ = -1; return -1;
        }
        bufs_[i].length = planes[0].length;
        bufs_[i].start  = mmap(NULL, planes[0].length,
                               PROT_READ | PROT_WRITE, MAP_SHARED,
                               fd_, planes[0].m.mem_offset);
        if (bufs_[i].start == MAP_FAILED) {
            printf("ERROR: mmap[%d] failed: %s\n", i, strerror(errno));
            ::close(fd_); fd_ = -1; return -1;
        }
        printf("  buf[%d]: size=%zu mmap=%p\n", i, bufs_[i].length, bufs_[i].start);
    }

    width_    = width;
    height_   = height;
    num_bufs_ = num_bufs_;
    printf("V4L2Capture opened: %s %dx%d NV12, %d buffers\n",
           device, width_, height_, num_bufs_);
    return 0;
}

int V4L2Capture::start() {
    // 预先把所有 buffer 入队
    for (int i = 0; i < num_bufs_; i++) {
        struct v4l2_plane planes[1];
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        memset(planes, 0, sizeof(planes));
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_MMAP;
        buf.index    = i;
        buf.m.planes = planes;
        buf.length   = 1;
        if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            printf("ERROR: VIDIOC_QBUF[%d] failed: %s\n", i, strerror(errno));
            return -1;
        }
    }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (xioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        printf("ERROR: VIDIOC_STREAMON failed: %s\n", strerror(errno));
        return -1;
    }
    streaming_ = true;
    printf("V4L2 STREAMON OK\n");
    return 0;
}

int V4L2Capture::grab_frame(uint8_t** data, size_t* size) {
    // select 等待帧就绪（超时 2 秒）
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);
    struct timeval tv = {2, 0};
    int r = select(fd_ + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) {
        printf("ERROR: select timeout or error: %s\n", r == 0 ? "timeout" : strerror(errno));
        return -1;
    }

    struct v4l2_plane planes[1];
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));
    buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory   = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.length   = 1;
    if (xioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
        printf("ERROR: VIDIOC_DQBUF failed: %s\n", strerror(errno));
        return -1;
    }

    cur_buf_idx_ = buf.index;
    *data = (uint8_t*)bufs_[cur_buf_idx_].start;
    *size = planes[0].bytesused > 0 ? planes[0].bytesused : bufs_[cur_buf_idx_].length;
    return 0;
}

int V4L2Capture::release_frame() {
    if (cur_buf_idx_ < 0) return 0;

    struct v4l2_plane planes[1];
    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    memset(planes, 0, sizeof(planes));
    buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory   = V4L2_MEMORY_MMAP;
    buf.index    = cur_buf_idx_;
    buf.m.planes = planes;
    buf.length   = 1;
    if (xioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        printf("ERROR: VIDIOC_QBUF release failed: %s\n", strerror(errno));
        return -1;
    }
    cur_buf_idx_ = -1;
    return 0;
}

int V4L2Capture::stop() {
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    xioctl(fd_, VIDIOC_STREAMOFF, &type);
    streaming_ = false;
    return 0;
}

void V4L2Capture::close() {
    for (int i = 0; i < num_bufs_; i++) {
        if (bufs_[i].start && bufs_[i].start != MAP_FAILED) {
            munmap(bufs_[i].start, bufs_[i].length);
            bufs_[i].start = nullptr;
        }
    }
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}
