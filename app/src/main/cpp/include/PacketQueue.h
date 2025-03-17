#ifndef ANDROIDPLAYER_PACKETQUEUE_H
#define ANDROIDPLAYER_PACKETQUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
extern "C" {
#include <libavcodec/avcodec.h>
}

class PacketQueue {
public:
    std::queue<AVPacket*> queue;
    std::mutex mtx;
    std::condition_variable cond;
    bool finished;


public:
    PacketQueue();
    ~PacketQueue();

    void push(AVPacket* pkt);

    bool pop(AVPacket* pkt);

    void setFinished(bool finished);

    bool isFinished();
};

// 音频队列
class SafeQueue {

public:
    void push(uint8_t* data, size_t size);
    std::unique_ptr<uint8_t[]> pop();
    bool empty() const;

public:
    std::queue<std::unique_ptr<uint8_t[]>> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
};

#endif //ANDROIDPLAYER_PACKETQUEUE_H
