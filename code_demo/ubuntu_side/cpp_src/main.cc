/*
 * main.cc — camera_yolo11_3core
 *
 * 编译方法（在 host 上，从项目根目录执行）:
 *   export CC=aarch64-linux-gnu-gcc
 *   export CXX=aarch64-linux-gnu-g++
 *   mkdir -p build_3core && cd build_3core
 *   cmake ../src/cpp/camera_yolo11_3core/ \
 *       -DCMAKE_SYSTEM_NAME=Linux \
 *       -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
 *       -DTARGET_SOC=rk3588 \
 *       -DCMAKE_BUILD_TYPE=Release
 *   make -j$(nproc)
 *
 * 使用方法（板子上执行）:
 *   # 相机模式（三核并发，受 ISP 30FPS 限制）
 *   ./camera_yolo11_3core \
 *       --model_path /userdata/yolo11n_rk3588.rknn \
 *       --num_frames 200 \
 *       --save_every 30
 *
 *   # NPU Benchmark 模式（内存喂帧，测纯 NPU 吞吐上限）
 *   ./camera_yolo11_3core \
 *       --model_path /userdata/yolo11n_rk3588.rknn \
 *       --benchmark \
 *       --num_frames 300
 *
 * 说明:
 *   三核 NPU 并发管线：
 *     [采集线程]    V4L2 DQBUF → memcpy → ring_buf(depth=4) → sem_post
 *     [推理线程×3]  sem_wait → dequeue → inference_yolo11_model(core 0/1/2) → stats
 *   Benchmark 模式跳过 V4L2，主线程循环往队列里推送同一帧，去除 ISP 帧率瓶颈
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/stat.h>

#include "../camera_yolo11_mt/v4l2_capture.h"
#include "yolo11.h"
#include "image_utils.h"
#include "image_drawing.h"
#include "file_utils.h"

// ─── 参数 ─────────────────────────────────────────────────────
#define CAMERA_DEV    "/dev/video22"
#define CAMERA_WIDTH  3840
#define CAMERA_HEIGHT 2160
#define V4L2_NUM_BUFS 4
#define NUM_NPU_CORES 3
#define RING_DEPTH    4          // 环形队列深度
#define RESULTS_DIR   "/userdata/results3"

// ─── 环形队列 ─────────────────────────────────────────────────
struct RingBuf {
    uint8_t* slots[RING_DEPTH];  // 预分配 NV12 帧槽位
    size_t   frame_size;
    int      head;               // 写指针（采集线程独占）
    int      tail;               // 读指针（推理线程共享，需 mutex）
    pthread_mutex_t tail_mutex;
    sem_t    filled;             // 可用帧计数
    sem_t    empty;              // 空闲槽位计数
    volatile bool stop;
};

static int ring_enqueue(RingBuf* rb, const uint8_t* data) {
    if (sem_trywait(&rb->empty) != 0) {
        // 队列已满：丢弃最老帧（直接覆盖），不阻塞
        pthread_mutex_lock(&rb->tail_mutex);
        rb->tail = (rb->tail + 1) % RING_DEPTH;
        pthread_mutex_unlock(&rb->tail_mutex);
        // tail 前进 = 丢弃一帧，对应 filled-- 和 empty 不变（复用其 slot）
        sem_trywait(&rb->filled);   // 移除被丢弃帧的 filled 计数
        // 此时 empty 仍为 0，但 head 槽位已由 tail 让出，可直接写入
    }
    memcpy(rb->slots[rb->head], data, rb->frame_size);
    rb->head = (rb->head + 1) % RING_DEPTH;
    sem_post(&rb->filled);
    return 0;
}

static int ring_dequeue(RingBuf* rb, uint8_t** out_data) {
    // 等待可用帧（最多 2 秒）
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 2;
    if (sem_timedwait(&rb->filled, &ts) != 0) return -1;

    pthread_mutex_lock(&rb->tail_mutex);
    *out_data = rb->slots[rb->tail];
    rb->tail  = (rb->tail + 1) % RING_DEPTH;
    pthread_mutex_unlock(&rb->tail_mutex);

    sem_post(&rb->empty);
    return 0;
}

// ─── 全局状态 ─────────────────────────────────────────────────
static volatile bool g_stop = false;
static void sig_handler(int) { g_stop = true; }

// ─── 时间工具 ─────────────────────────────────────────────────
static double get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

// ─── 推理线程参数 ─────────────────────────────────────────────
struct InferArgs {
    int                 core_id;     // 0/1/2
    RingBuf*            ring;
    rknn_app_context_t* rknn_ctx;
    int                 save_every;
    int                 num_frames;  // 总帧数上限（>0 有效）
    // 统计（由推理线程填写）
    int                 frame_count;
    double              total_infer_ms;
};

// ─── 全局帧计数（三个核共享，用于 FPS 统计和保存控制）───────
static volatile int  g_total_frames   = 0;
static volatile int  g_total_save_cnt = 0;
static pthread_mutex_t g_stats_mutex  = PTHREAD_MUTEX_INITIALIZER;
static double        g_t_start        = 0;

// FPS 滑动窗口（30 个 end-to-end 时间戳）
#define FPS_WIN 30
static double g_fps_ts[FPS_WIN];
static int    g_fps_idx = 0;

static double calc_fps() {
    int n = (g_total_frames < FPS_WIN) ? g_total_frames : FPS_WIN;
    if (n < 2) return 0;
    double oldest = g_fps_ts[(g_fps_idx - n + FPS_WIN) % FPS_WIN];
    double newest = g_fps_ts[(g_fps_idx - 1 + FPS_WIN) % FPS_WIN];
    return (n - 1) * 1000.0 / (newest - oldest);
}

// ─── 采集线程 ─────────────────────────────────────────────────
struct CaptureArgs {
    RingBuf*     ring;
    V4L2Capture* cap;
    int          num_frames;   // -1 无限
    bool         benchmark;    // true: 跳过 V4L2，用第一帧循环
};

static void* capture_thread(void* arg) {
    CaptureArgs* a    = (CaptureArgs*)arg;
    RingBuf*     ring = a->ring;
    int          cnt  = 0;

    if (a->benchmark) {
        // Benchmark 模式：从 V4L2 取 1 帧后循环喂给推理线程
        uint8_t* src; size_t sz;
        if (a->cap->grab_frame(&src, &sz) != 0) {
            printf("[capture] ERROR: failed to grab benchmark frame\n");
            ring->stop = true;
            return NULL;
        }
        uint8_t* frame_copy = (uint8_t*)malloc(sz);
        memcpy(frame_copy, src, sz);
        a->cap->release_frame();
        printf("[capture] Benchmark mode: looping single frame at max speed\n");

        // Benchmark: 持续喂帧直到推理线程达到 num_frames 目标（g_stop=true）
        while (!g_stop && !ring->stop) {
            ring_enqueue(ring, frame_copy);
            cnt++;
        }
        free(frame_copy);
    } else {
        // 相机模式
        while (!g_stop && !ring->stop) {
            uint8_t* src; size_t sz;
            if (a->cap->grab_frame(&src, &sz) != 0) {
                printf("[capture] ERROR: grab_frame failed\n");
                break;
            }
            ring_enqueue(ring, src);
            a->cap->release_frame();
            cnt++;
        }
    }

    // 通知推理线程退出：多 post 几次 filled 让它们解除阻塞
    ring->stop = true;
    for (int i = 0; i < NUM_NPU_CORES * 2; i++) sem_post(&ring->filled);
    printf("[capture] thread exit (%d frames enqueued)\n", cnt);
    return NULL;
}

// ─── 推理线程 ─────────────────────────────────────────────────
static void* infer_thread(void* arg) {
    InferArgs* a = (InferArgs*)arg;

    printf("[core %d] thread started\n", a->core_id);

    while (!g_stop) {
        uint8_t* frame_data = NULL;
        if (ring_dequeue(a->ring, &frame_data) != 0) {
            if (a->ring->stop) break;
            continue;
        }
        if (a->ring->stop && frame_data == NULL) break;

        // 构造 NV12 image_buffer_t
        image_buffer_t img;
        memset(&img, 0, sizeof(img));
        img.width     = CAMERA_WIDTH;
        img.height    = CAMERA_HEIGHT;
        img.format    = IMAGE_FORMAT_YUV420SP_NV12;
        img.virt_addr = frame_data;
        img.size      = (int)a->ring->frame_size;

        double t0 = get_time_ms();
        object_detect_result_list od_results;
        int ret = inference_yolo11_model(a->rknn_ctx, &img, &od_results);
        double infer_ms = get_time_ms() - t0;

        if (ret != 0) {
            printf("[core %d] ERROR: inference ret=%d\n", a->core_id, ret);
            continue;
        }

        a->frame_count++;
        a->total_infer_ms += infer_ms;

        // 全局统计
        pthread_mutex_lock(&g_stats_mutex);
        g_total_frames++;
        double now = get_time_ms();
        g_fps_ts[g_fps_idx % FPS_WIN] = now;
        g_fps_idx++;
        int total = g_total_frames;
        double fps = calc_fps();

        if (total % 10 == 0) {
            printf("%6d  %7.1f  core%d  %10d  %9.1fms\n",
                   total, fps, a->core_id, od_results.count, infer_ms);
        }

        // 每 save_every 帧保存结果图
        g_total_save_cnt++;
        bool do_save = (g_total_save_cnt % a->save_every == 0);

        // 检查是否达到总帧数上限
        bool reached_limit = (a->num_frames > 0 && total >= a->num_frames);
        pthread_mutex_unlock(&g_stats_mutex);

        if (reached_limit) {
            g_stop = true;
            break;
        }

        if (do_save) {
            image_buffer_t rgb_img;
            memset(&rgb_img, 0, sizeof(rgb_img));
            rgb_img.width     = CAMERA_WIDTH;
            rgb_img.height    = CAMERA_HEIGHT;
            rgb_img.format    = IMAGE_FORMAT_RGB888;
            rgb_img.size      = CAMERA_WIDTH * CAMERA_HEIGHT * 3;
            rgb_img.virt_addr = (uint8_t*)malloc(rgb_img.size);
            if (rgb_img.virt_addr) {
                convert_image(&img, &rgb_img, NULL, NULL, 0);
                char text[128];
                for (int i = 0; i < od_results.count; i++) {
                    object_detect_result* det = &od_results.results[i];
                    draw_rectangle(&rgb_img, det->box.left, det->box.top,
                                   det->box.right - det->box.left,
                                   det->box.bottom - det->box.top, COLOR_BLUE, 4);
                    snprintf(text, sizeof(text), "%s %.0f%%",
                             coco_cls_to_name(det->cls_id), det->prop * 100);
                    draw_text(&rgb_img, text, det->box.left, det->box.top - 20,
                              COLOR_RED, 10);
                }
                char path[256];
                snprintf(path, sizeof(path), "%s/frame_%05d_core%d.jpg",
                         RESULTS_DIR, total, a->core_id);
                write_image(path, &rgb_img);
                free(rgb_img.virt_addr);
            }
        }
    }

    printf("[core %d] exit: %d frames, avg infer %.1f ms\n",
           a->core_id, a->frame_count,
           a->frame_count > 0 ? a->total_infer_ms / a->frame_count : 0.0);
    return NULL;
}

// ─── 命令行解析 ───────────────────────────────────────────────
static void parse_args(int argc, char** argv,
                       const char** model_path, int* num_frames,
                       int* save_every, bool* benchmark)
{
    *model_path  = "/userdata/yolo11n_rk3588.rknn";
    *num_frames  = 200;
    *save_every  = 30;
    *benchmark   = false;
    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "--model_path") && i+1 < argc) *model_path = argv[++i];
        else if (!strcmp(argv[i], "--num_frames") && i+1 < argc) *num_frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--save_every") && i+1 < argc) *save_every = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--benchmark"))                 *benchmark  = true;
    }
}

// ─── 主函数 ───────────────────────────────────────────────────
int main(int argc, char** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    const char* model_path;
    int num_frames, save_every;
    bool benchmark;
    parse_args(argc, argv, &model_path, &num_frames, &save_every, &benchmark);

    printf("model_path : %s\n", model_path);
    printf("num_frames : %d\n", num_frames);
    printf("save_every : %d\n", save_every);
    printf("benchmark  : %s\n", benchmark ? "ON (pure NPU throughput)" : "OFF (camera mode)");

    mkdir(RESULTS_DIR, 0755);
    init_post_process();

    // ── 初始化 3 个 RKNN 实例，各绑定一个 NPU 核 ──────────────
    static const rknn_core_mask CORE_MASKS[NUM_NPU_CORES] = {
        RKNN_NPU_CORE_0, RKNN_NPU_CORE_1, RKNN_NPU_CORE_2
    };
    rknn_app_context_t rknn_ctxs[NUM_NPU_CORES];
    memset(rknn_ctxs, 0, sizeof(rknn_ctxs));

    for (int i = 0; i < NUM_NPU_CORES; i++) {
        if (init_yolo11_model(model_path, &rknn_ctxs[i]) != 0) {
            printf("ERROR: init_yolo11_model[%d] failed\n", i);
            return -1;
        }
        int ret = rknn_set_core_mask(rknn_ctxs[i].rknn_ctx, CORE_MASKS[i]);
        if (ret < 0) {
            printf("ERROR: rknn_set_core_mask[%d] failed, ret=%d\n", i, ret);
        } else {
            printf("NPU Core %d bound (mask=0x%x)\n", i, CORE_MASKS[i]);
        }
    }
    printf("All 3 NPU cores initialized\n");

    // ── 打开摄像头 ────────────────────────────────────────────
    V4L2Capture cap;
    if (cap.open(CAMERA_DEV, CAMERA_WIDTH, CAMERA_HEIGHT, V4L2_NUM_BUFS) != 0) {
        printf("ERROR: V4L2Capture open failed\n");
        return -1;
    }
    if (cap.start() != 0) {
        printf("ERROR: V4L2Capture start failed\n");
        return -1;
    }

    // ── 预分配环形队列 ────────────────────────────────────────
    size_t frame_size = (size_t)CAMERA_WIDTH * CAMERA_HEIGHT * 3 / 2;
    RingBuf ring;
    memset(&ring, 0, sizeof(ring));
    ring.frame_size = frame_size;
    for (int i = 0; i < RING_DEPTH; i++) {
        ring.slots[i] = (uint8_t*)malloc(frame_size);
        if (!ring.slots[i]) {
            printf("ERROR: failed to allocate ring slot %d (%zu MB)\n",
                   i, frame_size / 1024 / 1024);
            return -1;
        }
    }
    pthread_mutex_init(&ring.tail_mutex, NULL);
    sem_init(&ring.filled, 0, 0);
    sem_init(&ring.empty,  0, RING_DEPTH);
    printf("Ring buffer: %d slots × %zu MB = %zu MB total\n",
           RING_DEPTH, frame_size / 1024 / 1024,
           (size_t)RING_DEPTH * frame_size / 1024 / 1024);

    // ── 启动线程 ──────────────────────────────────────────────
    CaptureArgs cap_args = { &ring, &cap, num_frames, benchmark };
    InferArgs   inf_args[NUM_NPU_CORES];
    for (int i = 0; i < NUM_NPU_CORES; i++) {
        inf_args[i] = { i, &ring, &rknn_ctxs[i], save_every, num_frames, 0, 0.0 };
    }

    g_t_start = get_time_ms();
    printf("\n%6s  %7s  %5s  %10s  %10s\n",
           "Frame", "FPS", "Core", "Detections", "Infer(ms)");

    pthread_t cap_tid, inf_tids[NUM_NPU_CORES];
    for (int i = 0; i < NUM_NPU_CORES; i++)
        pthread_create(&inf_tids[i], NULL, infer_thread, &inf_args[i]);
    pthread_create(&cap_tid, NULL, capture_thread, &cap_args);

    // 等待推理线程完成目标帧数
    pthread_join(cap_tid, NULL);
    for (int i = 0; i < NUM_NPU_CORES; i++)
        pthread_join(inf_tids[i], NULL);

    // ── 最终统计 ──────────────────────────────────────────────
    double elapsed_s = (get_time_ms() - g_t_start) / 1000.0;
    double stable_fps = g_total_frames > 0 ? g_total_frames / elapsed_s : 0;

    printf("\n=== 结果 ===\n");
    printf("总帧数   : %d\n", g_total_frames);
    printf("总耗时   : %.1f s\n", elapsed_s);
    printf("端到端FPS: %.1f\n", stable_fps);
    printf("\n各核统计:\n");
    for (int i = 0; i < NUM_NPU_CORES; i++) {
        double avg = inf_args[i].frame_count > 0
            ? inf_args[i].total_infer_ms / inf_args[i].frame_count : 0;
        printf("  Core %d: %d 帧, 平均推理 %.1f ms\n",
               i, inf_args[i].frame_count, avg);
    }
    printf("结果图保存于: %s/\n", RESULTS_DIR);

    // ── 清理 ──────────────────────────────────────────────────
    cap.stop(); cap.close();
    for (int i = 0; i < NUM_NPU_CORES; i++) release_yolo11_model(&rknn_ctxs[i]);
    deinit_post_process();
    for (int i = 0; i < RING_DEPTH; i++) free(ring.slots[i]);
    pthread_mutex_destroy(&ring.tail_mutex);
    sem_destroy(&ring.filled);
    sem_destroy(&ring.empty);

    return 0;
}
