cmake_minimum_required(VERSION 3.22.1)


project("androidplayer")


set(ffmpeg_lib_dir ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI})
set(ffmpeg_head_dir ${CMAKE_SOURCE_DIR}/include/)

set(FFMPEG_LIB_DIR ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI})
set(FFMPEG_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include/)
include_directories(${ffmpeg_head_dir}/include
                    ${FFMPEG_INCLUDE_DIR}
)
find_library(
        log-lib
        log
)

#add_library(ffmpeg SHARED IMPORTED)
#set_target_properties(ffmpeg PROPERTIES IMPORTED_LOCATION ${ffmpeg_lib_dir}/libffmpeg-meikangjian.so)

# 核心工具库（必须）
add_library(avutil SHARED IMPORTED)
set_target_properties(avutil PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/libavutil.so)
# 声明动态库
add_library(avcodec SHARED IMPORTED)
set_target_properties(avcodec PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/libavcodec.so)
# 媒体容器处理库（文件解析必需）
add_library(avformat SHARED IMPORTED)
set_target_properties(avformat PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/libavformat.so)
# 图像缩放与格式转换库（画面渲染必需）
add_library(swscale SHARED IMPORTED)
set_target_properties(swscale PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/libswscale.so)
#
add_library(swresample SHARED IMPORTED)
set_target_properties(swresample PROPERTIES
        IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/libswresample.so)



add_definitions("-DDYNAMIC_ES2") # DDYNAMIC_ES2
set(OPENGL_LIB GLESv2) # GLESv2


add_library(${CMAKE_PROJECT_NAME} SHARED
        # List C/C++ source files with relative paths to this CMakeLists.txt.
        AAudioRender.cpp
        ANWRender.cpp
        ffmpegDecoder.cpp
        PacketQueue.cpp
        nativePlayer.cpp
        OpenGLRenderer.cpp
)

target_link_libraries(${CMAKE_PROJECT_NAME}
        avutil
        avcodec
        avformat
        swscale
        swresample
        android
        ${OPENGL_LIB} # OPENGL库，渲染图像
        EGL # EGL库，中间接口
        aaudio
        log
#        ffmpeg
        ${log-lib}
        m)