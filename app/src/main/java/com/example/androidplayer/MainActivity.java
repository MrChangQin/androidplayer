package com.example.androidplayer;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.Message;
import android.provider.Settings;
import android.util.Log;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.View;
import android.widget.Button;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;


import java.io.File;


import com.example.androidplayer.databinding.ActivityMainBinding;


public class MainActivity extends AppCompatActivity {
    // Used to load the 'androidplayer' library on application startup.
    static {
        System.loadLibrary("androidplayer");
        System.loadLibrary("avutil");
        System.loadLibrary("avcodec");
        System.loadLibrary("avformat");
        System.loadLibrary("swscale");
        System.loadLibrary("swresample");
//        System.loadLibrary("ffmpeg"); // 单个so库编译存在找不到函数的问题，编了三次
    }

    private Player player;
    private Handler mHandler;
    private SeekBar mSeekBar;

    public native int ffmpegDecodethread(String inputFile, String outputFile); // 使用多线程
    public native int ffmpegDecodernothread(String inputFile, String outputFile); // 不使用多线程
    public native String getFFmpegVersion(); //

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
            }
        }

        setContentView(ActivityMainBinding.inflate(getLayoutInflater()).getRoot());

        SurfaceView surfaceView;
        Surface surface;

        // 获取版本信息
        TextView tv2 = findViewById(R.id.sample_text3);
        tv2.setText(getFFmpegVersion());
        // 设置视频转码功能按钮
        Button btnConvert = findViewById(R.id.decode_button);
        btnConvert.setOnClickListener(v -> {
        Log.d("checkVideoPermission", "checkVideoPermission: true");
        File rootDir = Environment.getExternalStorageDirectory();  // 已过时但兼容旧系统
        String inputPath = rootDir.getAbsolutePath()  + "/1.mp4";
        String outputPath = rootDir.getAbsolutePath()  + "/1.yuv";
        Log.d("checkVideoPermission:", inputPath);
        Log.d("checkVideoPermission:", outputPath);
        int ret = ffmpegDecodethread(inputPath, outputPath); // 开始转码，多线程测试通过
            if (ret == 0) {
                Toast.makeText(MainActivity.this, "视频转换成功", Toast.LENGTH_SHORT).show();
            } else {
                Toast.makeText(MainActivity.this, "视频转换失败", Toast.LENGTH_SHORT).show();
            }
        });

        // 处理进度条更新
        mSeekBar = findViewById(R.id.seekBar); // 进度条
        mHandler = new Handler(Looper.getMainLooper()) { // 创建一个Handler，用于处理进度条更新，关联handler和主线程，在主线程中执行
            public void handleMessage(@NonNull Message msg) { // 重写handleMessage，处理子线程消息
                super.handleMessage(msg);
                if (msg.what == 1) { // 消息为1才处理
                    Bundle bundle = msg.getData();
                    int progress = bundle.getInt("progress"); // 获取progress对应的值
                    mSeekBar.setProgress(progress); // 更新进度条进度
                }
            }
        };

        // 创建播放器
        player = new Player();
        // 设置视频源
        File rootDir = Environment.getExternalStorageDirectory();
        player.setDataSource(rootDir.getAbsolutePath()  + "/kuangbiao.mp4");
        Log.d("Videopath:", rootDir.getAbsolutePath()  + "/kuangbiao.mp4");  // /storage/emulated/0/1.mp4

        // 设置surfaceView
        ((SurfaceView) findViewById(R.id.surfaceView)).getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override
            public void surfaceCreated(@NonNull SurfaceHolder holder) {
                player.setSurface(holder.getSurface()); // 设置player接受surface
                player.surfaceView = (SurfaceView) findViewById(R.id.surfaceView); // 设置surfaceView
            }
            @Override
            public void surfaceChanged(@NonNull SurfaceHolder holder, int format, int width, int height) {
            }
            @Override
            public void surfaceDestroyed(@NonNull SurfaceHolder holder) {
            }
        });

        // 开一个子线程progressThread用于更新进度条，周期性刷新进度条
        Thread progressThread = new Thread(() -> {
            int progress;
            while (true) {
                progress = (int) Math.round(player.getProgress() * 100); // *100在四舍五入，百分化
                Log.d("progressThread", "progressThread: " + player.getProgress());
                setSeekBar(progress); // 更新进度条
                try {
                    Thread.sleep(500);  // 线程休眠500ms，避免频繁更新进度条
                } catch (InterruptedException e) {
                    throw new RuntimeException(e);
                }
            }
        });

        // 播放和暂停
        Button play = findViewById(R.id.button);
        play.setText("播放");
        play.setOnClickListener(v -> {
            switch (player.getState()) { // 获取当前状态转换
                case None:
                case End:
                    player.start(); // 播放
                    ShowInfo(); // 显示视频信息
                    if (!progressThread.isAlive())
                        progressThread.start();  // 开启progressThread线程，更新进度条
                    play.setText("暂停");
                    break;
                case Playing:
                    player.pause(true); // 暂停
                    play.setText("播放");
                    break;
                case Paused:
                    player.pause(false); // 继续播放
                    play.setText("暂停");
                    break;
                default:
                    break;
            }
        });

        // 停止播放
        Button stop = findViewById(R.id.button2);
        stop.setOnClickListener(v -> {
            player.stop();  // 停止播放
            play.setText("播放");
            setSeekBar(0);
        });

        // 设置倍速播放
        Button speed = findViewById(R.id.button3);
        speed.setText("1x");
        speed.setOnClickListener(v -> {
            switch (speed.getText().toString()) {
                case "2x":
                    player.setSpeed(3);
                    speed.setText("3x");
                    break;
                case "3x":
                    player.setSpeed(0.5f);
                    speed.setText("0.5x");
                    break;
                case "0.5x":
                    player.setSpeed(1);
                    speed.setText("1x");
                    break;
                case "1x":
                    player.setSpeed(2);
                    speed.setText("2x");
                    break;
            }
        });

        // 设置进度条拖动的回调
        mSeekBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override // 重写onProgressChanged，处理进度条拖动事件
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (fromUser) // 当进度条被拖动时才执行
                    player.seek((double) progress / 100); // 设置进度
            }
            @Override
            public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {}
        });
    }

    // 更新进度条,SeekBar必须在主线程中更新，所以使用Handler
    private void setSeekBar(int progress) {
        Bundle bundle = new Bundle(); // 创建Bundle，用于传递数据
        bundle.putInt("progress", progress); // 将进度值传递给Bundle
        Message msg = new Message();
        msg.setData(bundle);
        msg.what = 1; // 设置消息类型为1
        mHandler.sendMessage(msg);
    }

    // 显示视频信息
    private void ShowInfo() {
        MediaInfo mediaInfo = player.mediaInfo;
        if (mediaInfo != null) {
            String info = "视频信息：\n" +
                    "视频尺寸：" + mediaInfo.videoWidth + "x" + mediaInfo.videoHeight + "\n" +
                    "视频时长：" + mediaInfo.videoDuration + "秒\n" +
                    "视频编码：" + mediaInfo.videoCodec + "\n";
            TextView infotex = findViewById(R.id.textView1);
            infotex.setText(info);
            String info2 = "音频信息：\n" +
                    "音频采样率：" + mediaInfo.audioSampleRate + "Hz\n" +
                    "音频声道数：" + mediaInfo.audioChannels + "\n" +
                    "音频编码：" + mediaInfo.audioCodec + "\n";
            TextView infotex2 = findViewById(R.id.textView2);
            infotex2.setText(info2);
        }
    }
}