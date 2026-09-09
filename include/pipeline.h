#pragma once
#include "audio.h"
#include "engine.h"
#include "segmenter.h"
#include <chrono>
#include <functional>
#include <thread>

struct SubtitleEvent {
    uint64_t id=0;
    bool final=false;
    std::string text;
    double start=0,end=0,compute_ms=0;
};
class StreamingVad {
    SpeechVad& vad_;
    Segmenter segmenter_;
    std::vector<float> history_=std::vector<float>(12800,0);
    std::vector<float> pending_;
public:
    explicit StreamingVad(SpeechVad& vad):vad_(vad){}
    void reset(){history_.assign(12800,0);pending_.clear();segmenter_.reset();}
    std::vector<Segment> accept(const float* samples,size_t count);
};
struct PipelineConfig {
    std::string model,vad,log_dir;
    int threads=8;
};
class Pipeline {
    PipelineConfig config_;
    std::function<void(SubtitleEvent)> on_text_;
    std::function<void(std::string)> on_status_;
    AudioInput input_;
    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> epoch_{0};
    std::thread worker_;
    void run();
public:
    Pipeline(PipelineConfig config,std::function<void(SubtitleEvent)> text,std::function<void(std::string)> status);
    ~Pipeline();
    void start();
    void pause(bool paused);
    bool paused() const {return input_.paused.load();}
};
