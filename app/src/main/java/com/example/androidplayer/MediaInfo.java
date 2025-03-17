package com.example.androidplayer;

public class MediaInfo {
    public int videoWidth;
    public int videoHeight;
    public double videoDuration;
    public String videoCodec; // 视频编码格式
    public int audioSampleRate;
    public int audioChannels;
    public double audioDuration;
    public String audioCodec; // 音频编码格式

    public MediaInfo(int videoWidth, int videoHeight, double videoDuration, String videoCodec,
                     int audioSampleRate, int audioChannels, String audioCodec) {
        this.videoWidth = videoWidth;
        this.videoHeight = videoHeight;
        this.videoDuration = videoDuration;
        this.videoCodec = videoCodec;
        this.audioSampleRate = audioSampleRate;
        this.audioChannels = audioChannels;
        this.audioCodec = audioCodec;
    }
}
