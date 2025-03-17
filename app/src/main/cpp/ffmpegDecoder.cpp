// 用于实现多线程的解码过程，包括视频解封装和视频解码，并将解码后的帧转换为 YUV 格式并写入输出文件。
#include <jni.h>
#include <android/log.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <fstream>
#include <iostream>

extern "C" {
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavformat/avio.h>
#include <libavutil/imgutils.h>
}

#define LOG_TAG "ffmpegDecoder"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// 全局队列及同步相关变量
std::mutex packet_queue_mutex;
std::condition_variable packet_queue_cv;
std::queue<AVPacket*> packet_queue;
bool stop_threads = false;

// 线程1：负责解封装视频流，将视频包放入共享队列中
void demux_thread(JNIEnv *env, AVFormatContext* fmt_ctx, int video_stream_index) {
    AVPacket* pkt = av_packet_alloc();
    while (!stop_threads) {
        if (av_read_frame(fmt_ctx, pkt) < 0) {
            break; // 读取结束
        }
        if (pkt->stream_index == video_stream_index) {
            // 加锁后将数据包加入队列
            {
                std::lock_guard<std::mutex> lock(packet_queue_mutex);
                // 这里需要拷贝 pkt 内容，因为后续会释放原 pkt 内存
                AVPacket* pkt_copy = av_packet_alloc();
                av_packet_ref(pkt_copy, pkt);
                packet_queue.push(pkt_copy);
            }
            packet_queue_cv.notify_one(); // 通知解码线程
        }
        av_packet_unref(pkt); // 非视频流包释放
    }
    av_packet_free(&pkt);
}

// 线程2：负责解码视频帧，将解码后的帧转换为 YUV 格式并写入输出文件
void decode_thread(AVCodecContext* codec_ctx, SwsContext* sws_ctx, FILE* out_file, int width, int height) {
    AVFrame* frame = av_frame_alloc();
    AVFrame* yuv_frame = av_frame_alloc();
    // 为 YUV420P 帧分配缓冲区
    uint8_t* yuv_buffer = new uint8_t[av_image_get_buffer_size(AV_PIX_FMT_YUV420P, width, height, 1)];
    av_image_fill_arrays(yuv_frame->data, yuv_frame->linesize, yuv_buffer,
                         AV_PIX_FMT_YUV420P, width, height, 1);

    while (true) {
        AVPacket* pkt = nullptr;
        {   // 线程同步等待数据包
            std::unique_lock<std::mutex> lock(packet_queue_mutex);
            packet_queue_cv.wait(lock, [] { return !packet_queue.empty() || stop_threads; });
            if (stop_threads && packet_queue.empty()) {
                break;
            }
            pkt = packet_queue.front();
            packet_queue.pop();
        }
        // 将数据包发送到解码器
        int ret = avcodec_send_packet(codec_ctx, pkt);
        if (ret < 0) {
            std::cerr << "发送数据包失败" << std::endl;
            av_packet_free(&pkt);
            continue;
        }
        // 循环接收解码后的帧
        while (ret >= 0) {
            ret = avcodec_receive_frame(codec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                std::cerr << "解码错误" << std::endl;
                break;
            }
            // 将解码后的帧转换为 YUV420P 格式
            sws_scale(sws_ctx,
                      frame->data, frame->linesize, 0, frame->height,
                      yuv_frame->data, yuv_frame->linesize);
            // 将 YUV 数据写入输出文件
            for (int i = 0; i < 3; i++) {
                int lines = (i == 0) ? height : height / 2;
                for (int j = 0; j < lines; j++) {
                    fwrite(yuv_frame->data[i] + j * yuv_frame->linesize[i],
                           1, yuv_frame->linesize[i], out_file);
                }
            }
        }
        av_packet_free(&pkt);
    }
    delete[] yuv_buffer;
    av_frame_free(&frame);
    av_frame_free(&yuv_frame);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_example_androidplayer_MainActivity_ffmpegDecodethread(JNIEnv *env, jobject thiz, jstring inputFile, jstring outputFile) {
    const char* input_file = env->GetStringUTFChars(inputFile, nullptr);
    const char* output_file = env->GetStringUTFChars(outputFile, nullptr);

    // 初始化 FFmpeg
    av_register_all(); // avformat_open_input自动初始化

    // 打开输入文件
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, input_file, nullptr, nullptr) < 0) {
        std::cerr << "无法打开输入文件" << std::endl;
        return -1;
    }
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "无法获取流信息" << std::endl;
        return -1;
    }

    // 查找视频流索引
    int video_stream_index = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            // 获取视频流
            AVCodecParameters *parameters = fmt_ctx->streams[i]->codecpar;
            int width = parameters->width;
            int height = parameters->height;
            LOGI("宽度Width：%d", width);
            LOGI("高度Height：%d", height);

//            break;
        } else if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            // 获取音频流
            AVCodecParameters *parameters = fmt_ctx->streams[i]->codecpar;
            LOGI("音频采样率：%d", parameters->sample_rate);
            LOGI("音频通道数：%d", parameters->channels);
            LOGI("音频采样位数：%d", parameters->bits_per_coded_sample);
        }
    }

    if (video_stream_index == -1) {
        std::cerr << "未找到视频流" << std::endl;
        return -1;
    }

    // 查找解码器并创建解码器上下文
    AVCodecParameters* codec_params = fmt_ctx->streams[video_stream_index]->codecpar;
    AVCodec* codec = avcodec_find_decoder(codec_params->codec_id); // 获取对应解码器
    if (!codec) {
        std::cerr << "找不到解码器" << std::endl;
        return -1;
    }
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec); // 根据解码器创建解码器上下文
    avcodec_parameters_to_context(codec_ctx, codec_params); // 将视频流参数拷贝到解码器上下文
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "无法打开解码器" << std::endl;
        return -1;
    }

    // 初始化 SWS 上下文，用于像素格式转换（转换为 YUV420P）
    SwsContext* sws_ctx = sws_getContext(
            codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
            codec_ctx->width, codec_ctx->height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

    // 打开输出文件
    FILE* out_file = fopen(output_file, "wb");
    if (!out_file) {
        std::cerr << "无法创建输出文件" << std::endl;
        return -1;
    }

    // 重置全局停止标志和队列状态
    stop_threads = false;
    while (!packet_queue.empty()) {
        packet_queue.pop();
    }

    // 启动两个线程
    std::thread t1(demux_thread, env, fmt_ctx, video_stream_index);
    std::thread t2(decode_thread, codec_ctx, sws_ctx, out_file, codec_ctx->width, codec_ctx->height);

    t1.join();
    // 当解封装线程结束后，设置停止标志，并通知等待的解码线程退出
    stop_threads = true;
    packet_queue_cv.notify_all();
    t2.join();

    // 释放资源
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    sws_freeContext(sws_ctx);
    fclose(out_file);

    std::cout << "解码完成！" << std::endl;
    return 0;
}

// 获取 FFmpeg 版本信息
extern "C" JNIEXPORT jstring JNICALL
Java_com_example_androidplayer_MainActivity_getFFmpegVersion(JNIEnv* env, jobject /* this */) {
    const char* version = av_version_info();
    return env->NewStringUTF(version ? version : "Unknown");
}

// 单线程解码版本：解封装和解码在同一线程中完成
extern "C" JNIEXPORT jint JNICALL
Java_com_example_androidplayer_MainActivity_ffmpegDecodernothread(JNIEnv *env, jobject thiz, jstring inputFile, jstring outputFile) {
    const char* input_file = env->GetStringUTFChars(inputFile, nullptr);
    const char* output_file = env->GetStringUTFChars(outputFile, nullptr);

    av_register_all();

    // 1. 打开输入文件并创建格式上下文
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, input_file, nullptr, nullptr) < 0) {
        std::cerr << "无法打开输入文件" << std::endl;
        return -1;
    }
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        std::cerr << "无法获取流信息" << std::endl;
        return -1;
    }

    // 2. 查找视频流索引
    int video_stream_index = -1;
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
            break;
        }
    }
    if (video_stream_index == -1) {
        std::cerr << "未找到视频流" << std::endl;
        return -1;
    }

    // 3. 查找解码器并创建解码器上下文
    AVCodecParameters* codec_params = fmt_ctx->streams[video_stream_index]->codecpar;
    AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
        std::cerr << "找不到解码器" << std::endl;
        return -1;
    }
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, codec_params);
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        std::cerr << "无法打开解码器" << std::endl;
        return -1;
    }

    // 4. 分配帧和数据包结构
    AVFrame* frame = av_frame_alloc();
    AVFrame* yuv_frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();

    // 5. 初始化 SWS 上下文用于像素格式转换
    SwsContext* sws_ctx = sws_getContext(
            codec_ctx->width, codec_ctx->height, codec_ctx->pix_fmt,
            codec_ctx->width, codec_ctx->height, AV_PIX_FMT_YUV420P,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

    // 6. 为 YUV 帧分配缓冲区
    uint8_t* yuv_buffer = new uint8_t[av_image_get_buffer_size(
            AV_PIX_FMT_YUV420P, codec_ctx->width, codec_ctx->height, 1)];
    av_image_fill_arrays(yuv_frame->data, yuv_frame->linesize, yuv_buffer,
                         AV_PIX_FMT_YUV420P, codec_ctx->width, codec_ctx->height, 1);

    // 7. 打开输出文件
    FILE* out_file = fopen(output_file, "wb");
    if (!out_file) {
        std::cerr << "无法创建输出文件" << std::endl;
        return -1;
    }

    // 8. 解码主循环
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_index) {
            int ret = avcodec_send_packet(codec_ctx, pkt);
            if (ret < 0) {
                std::cerr << "发送数据包失败" << std::endl;
                continue;
            }
            while (ret >= 0) {
                ret = avcodec_receive_frame(codec_ctx, frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    std::cerr << "解码错误" << std::endl;
                    break;
                }
                sws_scale(sws_ctx,
                          frame->data, frame->linesize, 0, frame->height,
                          yuv_frame->data, yuv_frame->linesize);
                for (int i = 0; i < 3; i++) {
                    int lines = (i == 0) ? codec_ctx->height : codec_ctx->height / 2;
                    for (int j = 0; j < lines; j++) {
                        fwrite(yuv_frame->data[i] + j * yuv_frame->linesize[i],
                               1, yuv_frame->linesize[i], out_file);
                    }
                }
            }
        }
        av_packet_unref(pkt);
    }

    // 9. 清理资源
    delete[] yuv_buffer;
    av_frame_free(&frame);
    av_frame_free(&yuv_frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);
    sws_freeContext(sws_ctx);
    fclose(out_file);

    std::cout << "解码完成！" << std::endl;
    return 0;
}
