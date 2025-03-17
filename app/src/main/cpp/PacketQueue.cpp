#include "PacketQueue.h"
#include <cstring>


PacketQueue::PacketQueue() : finished(false) {}

PacketQueue::~PacketQueue() {
    while (!queue.empty()) {
        AVPacket* pkt = queue.front();
        queue.pop();
        av_packet_free(&pkt);
    }
}

void PacketQueue::push(AVPacket* pkt) {
    std::unique_lock<std::mutex> lock(mtx);

    AVPacket* new_pkt = av_packet_alloc();
    av_packet_ref(new_pkt, pkt);
    queue.push(new_pkt);
    cond.notify_one();
}

bool PacketQueue::pop(AVPacket* pkt) {
    std::unique_lock<std::mutex> lock(mtx);
    // 如果队列为空且还未结束，则等待
    while (queue.empty() && !finished) {
        cond.wait(lock);
    }
    if (!queue.empty()) {
        AVPacket* front_pkt = queue.front();
        queue.pop();
        // 交换数据到外部传入的pkt
        av_packet_move_ref(pkt, front_pkt);
        av_packet_free(&front_pkt);
        return true;
    }
    return false;
}

void PacketQueue::setFinished(bool finished) {
    std::unique_lock<std::mutex> lock(mtx);
    this->finished = finished;
    cond.notify_all();
}

bool PacketQueue::isFinished() {
    std::unique_lock<std::mutex> lock(mtx);
    return finished;
}


// PCM音频队列方法
void SafeQueue::push(uint8_t* data, size_t size) {
    std::unique_lock<std::mutex> lock(mutex_);
    std::unique_ptr<uint8_t[]> ptr(new uint8_t[size]);
    std::copy(data, data + size, ptr.get());
    queue_.push(std::move(ptr));
    cond_.notify_one();
}

std::unique_ptr<uint8_t[]> SafeQueue::pop() {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this]() { return !queue_.empty(); });
    std::unique_ptr<uint8_t[]> data = std::move(queue_.front());
    queue_.pop();
    return data;
}

bool SafeQueue::empty() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}


