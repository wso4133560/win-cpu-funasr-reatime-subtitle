#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Single producer (WASAPI), single consumer (VAD). Never blocks the audio callback.
template<size_t Capacity> class AudioRing {
    // Allocate once at construction, not on the callback or Windows' small stack.
    std::vector<float> data_=std::vector<float>(Capacity,0);
    alignas(64) std::atomic<size_t> write_{0};
    alignas(64) std::atomic<size_t> read_{0};
public:
    bool push(const float* data,size_t count) {
        auto w=write_.load(std::memory_order_relaxed);
        auto r=read_.load(std::memory_order_acquire);
        if(count>Capacity-(w-r)) return false;
        for(size_t i=0;i<count;++i) data_[(w+i)%Capacity]=data?data[i]:0;
        write_.store(w+count,std::memory_order_release);
        return true;
    }
    size_t pop(float* data,size_t count) {
        auto r=read_.load(std::memory_order_relaxed);
        auto w=write_.load(std::memory_order_acquire);
        count=std::min(count,w-r);
        for(size_t i=0;i<count;++i) data[i]=data_[(r+i)%Capacity];
        read_.store(r+count,std::memory_order_release);
        return count;
    }
    size_t available() const {return write_.load(std::memory_order_acquire)-read_.load(std::memory_order_relaxed);}
    void clear() {read_.store(write_.load(std::memory_order_acquire),std::memory_order_release);}
};
struct AudioInput {
    AudioRing<16000*10> ring;
    std::atomic<bool> paused{false};
    std::atomic<uint64_t> dropped{0}, received{0}, reroutes{0};
};
class LoopbackCapture {
public:
    explicit LoopbackCapture(AudioInput& sink);
    ~LoopbackCapture();
    void start();
    bool running() const;
    std::string device_name() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::vector<float> load_audio(const std::string& path);
void save_audio(const std::string& path,const std::vector<float>& samples);
