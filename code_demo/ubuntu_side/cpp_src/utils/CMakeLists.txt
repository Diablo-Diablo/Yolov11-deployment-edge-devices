cmake_minimum_required(VERSION 3.10)
project(utils)

# fileutils
add_library(fileutils STATIC file_utils.c)
target_include_directories(fileutils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# imagedrawing
add_library(imagedrawing STATIC image_drawing.c)
target_include_directories(imagedrawing PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# imageutils（依赖 RGA + JPEG turbo）
# rk3588 启用 IM2D handle 路径（硬件 NV12→RGB 缩放）
if (TARGET_SOC STREQUAL "rk3588")
    add_definitions(-DLIBRGA_IM2D_HANDLE)
endif()

add_library(imageutils STATIC image_utils.c)
target_include_directories(imageutils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${STB_INCLUDES}
    ${LIBRGA_INCLUDES}
    ${LIBJPEG_INCLUDES}
)
target_link_libraries(imageutils
    ${LIBRGA}
    ${LIBJPEG}
)
