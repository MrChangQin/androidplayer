#include <jni.h>
#include <string>
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <thread>
#include <aaudio/AAudio.h>
#include <algorithm>
#include <iostream>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>

#include "PacketQueue.h"
#include "OpenGLRenderer.h"
#include "AAudioRender.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libavformat/avio.h>
#include <libavutil/imgutils.h>
#include <libavutil/version.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
}


#define LOG_TAG "NativePlayer"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// 全局变量
static AVFormatContext* fmt_ctx;
static AVCodecContext* codec_ctx_video = nullptr; // 视频解码上下文
static AVCodecContext* codec_ctx_audio = nullptr; // 音频解码上下文
static int video_stream_index = -1;
static int audio_stream_index = -1;
static ANativeWindow* native_window = nullptr;
static PacketQueue packetQueue_video; // 视频队列
static PacketQueue packetQueue_audio; // 音频队列
//static PacketQueue packetQueue_PCM; // 音频帧队列
std::atomic<bool> isPaused(false); // 暂停控制
std::atomic<bool> isStopped(false); // 停止控制
std::atomic<float> playbackSpeed(1.0f); // 播放速度控制
SwsContext *sws_ctx;
SwrContext *swr_ctx;
SafeQueue safeQueue;  // 音频帧队列
double duration;

// 读数据包线程
void readThread(const char* input_file) {
    AVPacket* pkt = av_packet_alloc();
    if (!pkt) {
        LOGE("无法分配 AVPacket");
        packetQueue_video.setFinished(true);
        packetQueue_audio.setFinished(true);
        return;
    }
    while (av_read_frame(fmt_ctx, pkt) >= 0) {
        if (pkt->stream_index == video_stream_index) {
            packetQueue_video.push(pkt);
        } else if (pkt->stream_index == audio_stream_index) {
            packetQueue_audio.push(pkt);
        }
        av_packet_unref(pkt);
    }
    packetQueue_video.setFinished(true); // 设置视频队列为完成状态
    packetQueue_audio.setFinished(true); // 设置音频队列为完成状态
    av_packet_free(&pkt);
}

// 解码线程
void decodeVideo() {
    AVFrame* frame = av_frame_alloc();
    AVFrame* rgb_frame = av_frame_alloc();
    if (!frame || !rgb_frame) {
        return;
    }

    // 初始化SWS上下文
    sws_ctx = sws_getContext(codec_ctx_video->width, codec_ctx_video->height, codec_ctx_video->pix_fmt,
                                         codec_ctx_video->width, codec_ctx_video->height, AV_PIX_FMT_RGBA,
                                         SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_ctx) {
        av_frame_free(&frame);
        av_frame_free(&rgb_frame);
        return;
    }

    // 为RGB帧分配缓冲区
    int numBytes = av_image_get_buffer_size(AV_PIX_FMT_RGBA, codec_ctx_video->width, codec_ctx_video->height, 1);
    uint8_t* rgb_buffer = new uint8_t[numBytes];
    av_image_fill_arrays(rgb_frame->data, rgb_frame->linesize, rgb_buffer,
                         AV_PIX_FMT_RGBA, codec_ctx_video->width, codec_ctx_video->height, 1);

    // 初始化OpenGL环境，传入ANativeWindow
    if (!initOpenGL(native_window, codec_ctx_video->width, codec_ctx_video->height)) {
        LOGE("OpenGL 初始化失败");
    }

    // 设置ANativeWindow的缓冲区格式
    ANativeWindow_setBuffersGeometry(native_window, codec_ctx_video->width, codec_ctx_video->height, WINDOW_FORMAT_RGBA_8888);

    AVPacket* pkt = av_packet_alloc();
    while (packetQueue_video.pop(pkt)) {
        int ret = avcodec_send_packet(codec_ctx_video, pkt);
        if (ret < 0) {
            LOGE("发送数据包失败：%d", ret);
            av_packet_unref(pkt);
            continue;
        }
        while (ret >= 0) {
            ret = avcodec_receive_frame(codec_ctx_video, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                break;
            } else if (ret < 0) {
                LOGE("解码错误：%d", ret);
                break;
            }

            // 转为RGBA格式
            sws_scale(sws_ctx,
                      frame->data, frame->linesize, 0, frame->height,
                      rgb_frame->data, rgb_frame->linesize);

            // 停止控制
            if (isStopped) break;

            // 暂停控制
            while (isPaused) {
                av_usleep(10000);  // 线程休眠
            }
            // 倍速控制
            double frame_delay = 1 / (codec_ctx_video->framerate.num / (double)codec_ctx_video->framerate.den);
            frame_delay /= (playbackSpeed*10);  // 根据播放速度调整帧的显示时间间隔
            av_usleep((int)(frame_delay * 1000000));   // 等待帧的显示时间

            // 调用opengl渲染函数，不直接渲染到ANativeWindow
            renderFrame(rgb_frame->data[0], codec_ctx_video->width, codec_ctx_video->height);
        }
        av_packet_unref(pkt);
    }

    // 播放结束后，清理资源
    cleanupOpenGL();
    av_packet_free(&pkt);
    delete[] rgb_buffer;
    av_frame_free(&frame);
    av_frame_free(&rgb_frame);
    sws_freeContext(sws_ctx);

    // 清理资源
    avcodec_free_context(&codec_ctx_video);
    avformat_close_input(&fmt_ctx);
    if (native_window) {
        ANativeWindow_release(native_window);
        native_window = nullptr;
    }
}

int audioCallback(AAudioStream *stream, void *userData, void *audioData, int32_t numFrames) {
    SafeQueue *safeQueue_ = static_cast<SafeQueue*>(userData);
    // 计算每个帧的字节数，假设是 16 位立体声
    int bytesPerFrame = av_get_bytes_per_sample(AV_SAMPLE_FMT_S16) * 2;
    int bufferSize = numFrames * bytesPerFrame;
    // 如果队列为空，填充静音数据
    if (safeQueue_->empty()) {
        memset(audioData, 0, bufferSize);
        return 0;
    }
    // 从队列中取出音频数据
    auto audioFrame = safeQueue_->pop();
    // 计算可用数据的大小
    int availableSize = av_samples_get_buffer_size(nullptr, 2, numFrames, AV_SAMPLE_FMT_S16, 1);
    // 将数据复制到音频缓冲区
    if (availableSize <= bufferSize) {
        memcpy(audioData, audioFrame.get(), availableSize);
        // 如果可用数据小于缓冲区大小，剩余部分填充静音数据
        if (availableSize < bufferSize) {
            memset(static_cast<uint8_t*>(audioData) + availableSize, 0, bufferSize - availableSize);
        }
    } else {
        // 如果可用数据大于缓冲区大小，只复制缓冲区大小的数据
        memcpy(audioData, audioFrame.get(), bufferSize);
    }
    return 0;
}


// 音频解码
void decodeAudio() {
    AVFrame *audioFrame = av_frame_alloc(); // 申请一个AVFrame，用来装解码后的数据
    // 初始化重采样上下文
    swr_ctx = swr_alloc();
    uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    int out_sample_rate = codec_ctx_audio->sample_rate;

    swr_alloc_set_opts(swr_ctx, out_ch_layout, out_sample_fmt, out_sample_rate,
                       codec_ctx_audio->channel_layout, codec_ctx_audio->sample_fmt,
                       codec_ctx_audio->sample_rate, 0, nullptr);
    swr_init(swr_ctx);

    int out_channel_nb = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    uint8_t *out_buffer = (uint8_t *) av_malloc(44100*2);

    while (!packetQueue_audio.isFinished()) {// isstart
        AVPacket *audioPacket = av_packet_alloc(); // 申请一个AVPacket，用来装pakect
        // 从队列中获取音频数据包
        if (packetQueue_audio.pop(audioPacket) < 0) {
            continue; // 如果队列为空或发生错误，继续等待
        }
        LOGI("音频数据包大小：%d", audioPacket->size);
        int ret = avcodec_send_packet(codec_ctx_audio, audioPacket);
        ret = avcodec_receive_frame(codec_ctx_audio, audioFrame);
        if (ret != 0) {
            continue;
        }
        uint8_t *out_buffer = (uint8_t *) av_malloc(44100*2);
        swr_convert(swr_ctx, &out_buffer, 44100*2,
                    (const uint8_t **) audioFrame->data, audioFrame->nb_samples);
        int size = av_samples_get_buffer_size(nullptr, out_channel_nb,
                                              audioFrame->nb_samples, AV_SAMPLE_FMT_S16,1);

        safeQueue.push(out_buffer, size);

        LOGI("音频数据帧大小：%d", audioFrame->pkt_size);
        av_packet_free(&audioPacket);
        av_free(out_buffer);
    }
    swr_free(&swr_ctx);
    av_frame_free(&audioFrame);
    avcodec_close(codec_ctx_audio);
    avformat_close_input(&fmt_ctx);
    return;
}


// 播放器初始化
extern "C" JNIEXPORT jobject JNICALL
Java_com_example_androidplayer_Player_nativePlay(JNIEnv *env, jobject thiz, jstring inputFile, jobject surface) {
    // 获取输入文件路径和 ANativeWindow
    const char* input_file = env->GetStringUTFChars(inputFile, nullptr);
    native_window = ANativeWindow_fromSurface(env, surface);
    if (!native_window) {
        LOGE("无法获取 ANativeWindow");
        env->ReleaseStringUTFChars(inputFile, input_file);
        return nullptr;
    }
    if (avformat_open_input(&fmt_ctx, input_file, nullptr, nullptr) < 0) {
        ANativeWindow_release(native_window);
        env->ReleaseStringUTFChars(inputFile, input_file);
        return nullptr;
    }
    // 获取流信息
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        ANativeWindow_release(native_window);
        env->ReleaseStringUTFChars(inputFile, input_file);
        return nullptr;
    }
    // 查找视频流索引
    for (unsigned int i = 0; i < fmt_ctx->nb_streams; i++) {
        if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_stream_index = i;
//            break;
        } else if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            // 音频解码器
            audio_stream_index = i;
            AVCodecParameters *parameters = fmt_ctx->streams[i]->codecpar;
            LOGI("音频通道数：%d",parameters->channels);
            LOGI("音频采样率：%d",parameters->sample_rate);

            AVCodec *audio_dec = avcodec_find_decoder(parameters->codec_id);
            codec_ctx_audio = avcodec_alloc_context3(audio_dec);
            avcodec_parameters_to_context(codec_ctx_audio,parameters);
            avcodec_open2(codec_ctx_audio,audio_dec,0);
        }
    }

    if (video_stream_index == -1) {
        LOGE("未找到视频流");
        avformat_close_input(&fmt_ctx);
        ANativeWindow_release(native_window);
        env->ReleaseStringUTFChars(inputFile, input_file);
        return nullptr;
    }
    // 查找解码器
    AVCodecParameters* codec_params = fmt_ctx->streams[video_stream_index]->codecpar;
    AVCodecParameters* codec_params2 = fmt_ctx->streams[audio_stream_index]->codecpar;
    // 获取解码器参数
    int width = codec_params->width;
    int height = codec_params->height;

    int sampleRate = codec_params2->sample_rate;
    int channels = codec_params2->channels;
    const char*  audioCodec = avcodec_get_name(codec_params2->codec_id);

    // 自适应窗口的回调
    jclass david_player = env->GetObjectClass(thiz);
    jmethodID onSizeChange = env->GetMethodID(david_player, "onSizeChange", "(II)V");
    env->CallVoidMethod(thiz, onSizeChange, width, height);

    duration = fmt_ctx->duration / (double)AV_TIME_BASE;
    const char* codec_name = avcodec_get_name(codec_params->codec_id);

    AVCodec* codec = avcodec_find_decoder(codec_params->codec_id);
    if (!codec) {
        LOGE("找不到解码器");
        avformat_close_input(&fmt_ctx);
        ANativeWindow_release(native_window);
        env->ReleaseStringUTFChars(inputFile, input_file);
        return nullptr;
    }
    // 分配解码器上下文并打开解码器
    codec_ctx_video = avcodec_alloc_context3(codec);
    if (!codec_ctx_video) {
        LOGE("无法分配 AVCodecContext");
        avformat_close_input(&fmt_ctx);
        ANativeWindow_release(native_window);
        env->ReleaseStringUTFChars(inputFile, input_file);
        return nullptr;
    }
    if (avcodec_parameters_to_context(codec_ctx_video, codec_params) < 0) {
        LOGE("无法拷贝解码器参数到上下文");
        avcodec_free_context(&codec_ctx_video);
        avformat_close_input(&fmt_ctx);
        ANativeWindow_release(native_window);
        env->ReleaseStringUTFChars(inputFile, input_file);
        return nullptr;
    }
    if (avcodec_open2(codec_ctx_video, codec, nullptr) < 0) {
        LOGE("无法打开解码器");
        avcodec_free_context(&codec_ctx_video);
        avformat_close_input(&fmt_ctx);
        ANativeWindow_release(native_window);
        env->ReleaseStringUTFChars(inputFile, input_file);
        return nullptr;
    }

    // 创建 MediaInfo 对象并返回的回调
    jclass videoInfoClass = env->FindClass("com/example/androidplayer/MediaInfo");
//    jmethodID constructor = env->GetMethodID(videoInfoClass, "<init>", "(IIDLjava/lang/String;)V");
    jmethodID constructor = env->GetMethodID(videoInfoClass, "<init>", "(IIDLjava/lang/String;IILjava/lang/String;)V");
//    jobject videoInfo = env->NewObject(videoInfoClass, constructor, width, height, duration,
//                                       env->NewStringUTF(codec_name));
    jobject videoInfo = env->NewObject(videoInfoClass, constructor,
                                       width, height, duration, env->NewStringUTF(codec_name),
                                       sampleRate, channels, env->NewStringUTF(audioCodec));

    packetQueue_video.setFinished(false); // 设置队列为未结束
    
//    AAudioRender audioRender;
//    audioRender.configure(sampleRate, channels, AV_SAMPLE_FMT_S16);
//    audioRender.setCallback(audioCallback, &safeQueue);
//    audioRender.start();
    // detach读数据包和解码线程，放置阻塞主线程
    std::thread(readThread, input_file).detach();
    std::thread(decodeVideo).detach();
//    std::thread(decodeAudio).detach();
    env->ReleaseStringUTFChars(inputFile, input_file);

    return videoInfo;
}


// 暂停播放
extern "C"
JNIEXPORT void JNICALL
Java_com_example_androidplayer_Player_nativePause(JNIEnv *env, jobject thiz, jboolean p) {
    isPaused = (p == JNI_TRUE); // 设置暂停标志
}

// 跳转播放
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_androidplayer_Player_nativeSeek(JNIEnv *env, jobject thiz, jdouble position) {
    if (!fmt_ctx || position < 0)
        return -1;
    int64_t target_ts = (int64_t)(position * AV_TIME_BASE); // 将秒数转换为时间戳
    // 跳转到目标位置
    int ret = av_seek_frame(fmt_ctx, -1, target_ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0) {
        LOGE("跳转失败: %s", av_err2str(ret));
        return ret;
    }
    // 清空解码器缓冲区
    if (codec_ctx_video) {
        avcodec_flush_buffers(codec_ctx_video);
    }
    return 0;
}

// 停止播放，存在bug
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_androidplayer_Player_nativeStop(JNIEnv *env, jobject thiz) {
    isStopped = true;
    if (codec_ctx_video) {
        avcodec_close(codec_ctx_video);
        avcodec_free_context(&codec_ctx_video);
        codec_ctx_video = nullptr;
    }
    if (fmt_ctx) {
        avformat_close_input(&fmt_ctx);
        fmt_ctx = nullptr;
    }
    return 0;
}

// 设置播放速度
extern "C"
JNIEXPORT jint JNICALL
Java_com_example_androidplayer_Player_nativeSetSpeed(JNIEnv *env, jobject thiz, jfloat speed) {
    if (speed <= 0) {
        return -1; // 速度必须大于 0
    }
    playbackSpeed = speed; // 更新播放速度
    return 0;
}

// 获取播放进度，存在bug
extern "C" JNIEXPORT jdouble JNICALL
Java_com_example_androidplayer_Player_nativeGetPosition(JNIEnv *env, jobject thiz) {
    int64_t position = fmt_ctx->streams[video_stream_index]->cur_dts; // 获取当前视频流的时间戳（微秒）
    if (position == AV_NOPTS_VALUE) {// 如果获取失败，返回 -1
        return -1.0;
    }
    jdouble progress = position / (double)AV_TIME_BASE; // 微秒转秒
    LOGI("nativeGetPosition: %f", progress);
        return progress;
}

// 获取播放时长
extern "C" JNIEXPORT jdouble JNICALL
Java_com_example_androidplayer_Player_nativeGetDuration(JNIEnv *env, jobject thiz) {
    return duration;
}
