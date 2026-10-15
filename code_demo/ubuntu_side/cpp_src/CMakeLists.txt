cmake_minimum_required(VERSION 3.10)
project(camera_yolo11_3core)

# 固定目标平台 rk3588（aarch64 Linux 交叉编译）
if (NOT DEFINED TARGET_SOC)
    set(TARGET_SOC rk3588)
endif()

# 加载本地 3rdparty 和 utils（路径均相对于本 CMakeLists.txt）
add_subdirectory(3rdparty 3rdparty.out)
add_subdirectory(utils    utils.out)

set(YOLO11_CPP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/yolo11_cpp)
set(YOLO11_SRCS
    ${YOLO11_CPP_DIR}/postprocess.cc
    ${YOLO11_CPP_DIR}/rknpu2/yolo11.cc
)

# 安装时让可执行文件从 ../lib 找动态库
set(CMAKE_INSTALL_RPATH "$ORIGIN/../lib")

add_executable(camera_yolo11_3core
    main.cc
    v4l2_capture.cc
    ${YOLO11_SRCS}
)

target_include_directories(camera_yolo11_3core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${YOLO11_CPP_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/utils
    ${LIBRKNNRT_INCLUDES}
    ${LIBRGA_INCLUDES}
)

target_link_libraries(camera_yolo11_3core
    imageutils
    fileutils
    imagedrawing
    ${LIBRKNNRT}
    ${LIBRGA}
    pthread
    dl
    -static-libstdc++
    -static-libgcc
)

# cmake --install 时将二进制和 librknnrt.so 一起装到 rk3588_side/
install(TARGETS camera_yolo11_3core DESTINATION .)
