cmake_minimum_required(VERSION 3.10)
project(3rdparty)

# 仅保留 camera_yolo11_3core 实际使用的依赖，固定 aarch64 Linux

# STB image（header-only）
set(STB_PATH     ${CMAKE_CURRENT_SOURCE_DIR}/stb_image)
set(STB_INCLUDES ${STB_PATH} PARENT_SCOPE)

# JPEG turbo（静态库，供 imageutils 链接）
set(JPEG_PATH         ${CMAKE_CURRENT_SOURCE_DIR}/jpeg_turbo)
set(LIBJPEG           ${JPEG_PATH}/Linux/aarch64/libturbojpeg.a PARENT_SCOPE)
set(LIBJPEG_INCLUDES  ${JPEG_PATH}/include PARENT_SCOPE)

# RKNN Runtime（动态库，rk3588 使用 rknpu2）
set(RKNN_PATH         ${CMAKE_CURRENT_SOURCE_DIR}/rknpu2)
set(LIBRKNNRT         ${RKNN_PATH}/Linux/aarch64/librknnrt.so PARENT_SCOPE)
set(LIBRKNNRT_INCLUDES ${RKNN_PATH}/include PARENT_SCOPE)
install(PROGRAMS ${RKNN_PATH}/Linux/aarch64/librknnrt.so DESTINATION lib)

# RGA（静态库链接，动态库随包安装供板子使用）
set(RGA_PATH      ${CMAKE_CURRENT_SOURCE_DIR}/librga)
set(LIBRGA        ${RGA_PATH}/Linux/aarch64/librga.a PARENT_SCOPE)
set(LIBRGA_INCLUDES ${RGA_PATH}/include PARENT_SCOPE)
install(PROGRAMS  ${RGA_PATH}/Linux/aarch64/librga.so DESTINATION lib)
