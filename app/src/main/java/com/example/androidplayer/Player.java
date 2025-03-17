package com.example.androidplayer;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceView;
import android.widget.RelativeLayout;

import androidx.core.util.Pair;

import java.math.BigDecimal;

public class Player {

    static {
        System.loadLibrary("androidplayer");
    }

    private AudioTrack audioTrack;
    private long nativeContext;
    public enum PlayerState {
        None,
        Playing,
        Paused,
        End,
        Seeking
    }
    public Surface mSurface;
    public PlayerState mState = PlayerState.None;
    public String fileUri;

    public double duration;

    SurfaceView surfaceView; // 用于设置宽高
    public MediaInfo mediaInfo; // 视频信息

    public void setDataSource(String uri) {
        fileUri = uri;
    }

    public void setSurfaceView(SurfaceView surfaceView) {
        this.surfaceView = surfaceView;
    }

    public void setSurface(Surface surface) {
        mSurface = surface;
    }
    public void start() {
        mediaInfo = nativePlay(fileUri, mSurface);   // debug
        mState = PlayerState.Playing;
        duration = nativeGetDuration(); // 获取视频时长
    }
    public void pause(boolean p) {
        nativePause(p);
        if (p) {
            mState = PlayerState.Paused;
        } else {
            mState = PlayerState.Playing;
        }
    }
    public void stop() {
        nativeStop();
        mState = PlayerState.End;
    }
    public void seek(double position) {
        nativeSeek(position);
    }
    public double getProgress() {
        return nativeGetPosition() / duration;   // 当前秒数/总秒数，获取当前播放进度
    }
    public PlayerState getState() {
        return mState;
    }
    public void setSpeed(float speed) {
        nativeSetSpeed(speed);
    }
    public native MediaInfo nativePlay(String file, Surface surface); // private native void play(String file, Surface surface);
    private native void nativePause(boolean p); // 暂停
    private native int nativeSeek(double position);
    private native int nativeStop(); // 停止
    private native int nativeSetSpeed(float speed);
    private native double nativeGetPosition();
    private native double nativeGetDuration();


    // 创建音频播放对象
    public void createTrack(int sampleRate, int channelCount) {
        // 确定声道配置
        int channelConfig;
        if (channelCount == 1) {
            channelConfig = AudioFormat.CHANNEL_OUT_MONO;
        } else if (channelCount == 2) {
            channelConfig = AudioFormat.CHANNEL_OUT_STEREO;
        } else {
            channelConfig = AudioFormat.CHANNEL_OUT_MONO;
        }

        // 计算缓冲区大小
        int bufferSize = AudioTrack.getMinBufferSize(sampleRate, channelConfig, AudioFormat.ENCODING_PCM_16BIT);

        // 配置音频属性
        AudioAttributes audioAttributes = new AudioAttributes.Builder()
                .setUsage(AudioAttributes.USAGE_MEDIA) // 设置音频用途为媒体
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC) // 设置音频内容类型为音乐
                .build();

        // 配置音频格式
        AudioFormat audioFormat = new AudioFormat.Builder()
                .setSampleRate(sampleRate) // 设置采样率
                .setChannelMask(channelConfig) // 设置声道配置
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT) // 设置音频格式为 16 位 PCM
                .build();

        // 创建 AudioTrack 实例
        audioTrack = new AudioTrack.Builder()
                .setAudioAttributes(audioAttributes) // 设置音频属性
                .setAudioFormat(audioFormat) // 设置音频格式
                .setBufferSizeInBytes(bufferSize) // 设置缓冲区大小
                .setTransferMode(AudioTrack.MODE_STREAM) // 设置传输模式为流模式
                .build();

        // 启动播放
        audioTrack.play();
    }
    // 播放音频
    public void playTrack(byte[] buffer, int length) {
        Log.d("playTrack", "length:" + length);
        audioTrack.write(buffer, 0, length);
    }

    // 用于自适应视频宽高比
    public void onSizeChange(int width, int height) { // 动态宽高比，需要debug
        float ratio = width / (float) height;
        int sceenWidth = surfaceView.getContext().getResources().getDisplayMetrics().widthPixels;
        int videoWidth = 0;
        int videoHeight = 0;
        videoWidth = sceenWidth;
        videoHeight = (int) (sceenWidth / ratio);
        RelativeLayout.LayoutParams lp = new RelativeLayout.LayoutParams(videoWidth, videoHeight);
        // 设置控件大小
        surfaceView.setLayoutParams(lp);
    }
}
