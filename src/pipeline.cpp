#include "pipeline.h"
#include <array>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
using Clock=std::chrono::steady_clock;

std::vector<Segment> StreamingVad::accept(const float* samples,size_t count){
    pending_.insert(pending_.end(),samples,samples+count);
    std::vector<Segment> events;
    constexpr size_t block=2560,lookahead=640;
    while(pending_.size()>=block+lookahead){
        auto context=history_;
        context.insert(context.end(),pending_.begin(),pending_.begin()+block+lookahead);
        auto p=vad_.probabilities(context);
        for(size_t i=0;i<block/160;++i){
            size_t pos=history_.size()/160+i;
            if(pos>=p.size())throw std::runtime_error("VAD frame alignment error");
            if(auto s=segmenter_.feed(pending_.data()+i*160,p[pos]))events.push_back(std::move(*s));
        }
        history_.insert(history_.end(),pending_.begin(),pending_.begin()+block);
        history_.erase(history_.begin(),history_.end()-12800);
        pending_.erase(pending_.begin(),pending_.begin()+block);
    }
    return events;
}
static std::string stamp(double seconds){
    auto ms=std::max<int64_t>(0,static_cast<int64_t>(seconds*1000));
    char buf[64];snprintf(buf,sizeof(buf),"%02lld:%02lld:%02lld,%03lld",ms/3600000,(ms/60000)%60,(ms/1000)%60,ms%1000);return buf;
}
Pipeline::Pipeline(PipelineConfig c,std::function<void(SubtitleEvent)> text,std::function<void(std::string)> status)
    :config_(std::move(c)),on_text_(std::move(text)),on_status_(std::move(status)){}
Pipeline::~Pipeline(){stop_=true;if(worker_.joinable())worker_.join();}
void Pipeline::start(){worker_=std::thread([this]{run();});}
void Pipeline::pause(bool value){input_.paused=value;++epoch_;}
void Pipeline::run(){
    try{
        on_status_("Loading CPU models...");
        SenseVoice engine(config_.model,config_.threads);
        SpeechVad vad(config_.vad,2);
        StreamingVad stream(vad);
        LoopbackCapture capture(input_);
        std::filesystem::create_directories(config_.log_dir);
        auto session=std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        std::ofstream log(config_.log_dir+"/"+session+".tsv");
        std::ofstream srt(config_.log_dir+"/"+session+".srt");
        if(!log||!srt)throw std::runtime_error("Cannot write transcript logs");
        log<<"id\tfinal\tstart_s\tend_s\tcompute_ms\ttext\n";
        std::mutex mutex;
        std::condition_variable cv;
        SegmentQueue jobs;
        bool finished=false;
        // Epoch rejects results from audio captured before pause, reroute or overflow.
        uint64_t queue_epoch=epoch_.load();
        auto infer=std::jthread([&]{
            try{
                int srt_index=0;
                while(true){
                    std::optional<Segment> job;
                    uint64_t version;
                    {std::unique_lock lock(mutex);cv.wait_for(lock,std::chrono::milliseconds(50),[&]{return finished||!jobs.empty();});
                        if(finished||stop_)break;
                        job=jobs.pop();version=queue_epoch;}
                    if(!job)continue;
                    auto before=Clock::now();
                    auto text=engine.transcribe(job->audio);
                    auto elapsed=std::chrono::duration<double,std::milli>(Clock::now()-before).count();
                    if(version!=epoch_.load()||input_.paused||stop_)continue;
                    if(text.empty())continue;
                    log<<job->id<<'\t'<<job->final<<'\t'<<job->start<<'\t'<<job->end<<'\t'<<elapsed<<'\t'<<text<<'\n';log.flush();
                    if(job->final){srt<<++srt_index<<'\n'<<stamp(job->start)<<" --> "<<stamp(std::max(job->end,job->start+0.01))<<'\n'<<text<<"\n\n";srt.flush();}
                    on_text_({job->id,job->final,text,job->start,job->end,elapsed});
                }
            }catch(const std::exception& e){on_status_(std::string("Inference error: ")+e.what());stop_=true;}
        });
        // Always wake/join inference before stack-owned queue/model objects disappear.
        auto finish=[&]{ {std::lock_guard lock(mutex);finished=true;}cv.notify_all();if(infer.joinable())infer.join();};
        try{
            capture.start();
            on_status_("Listening to Windows output: "+capture.device_name());
            std::array<float,2560> audio{};
            uint64_t old_drops=0,old_route=0;
            auto last_packet=Clock::now();
            while(!stop_){
                const auto drops=input_.dropped.load(),route=input_.reroutes.load();
                if(drops!=old_drops||route!=old_route){old_drops=drops;old_route=route;++epoch_;on_status_("Audio discontinuity: buffer reset");}
                if(queue_epoch!=epoch_.load()){
                    stream.reset();input_.ring.clear();
                    std::lock_guard lock(mutex);jobs.clear();queue_epoch=epoch_.load();
                }
                if(!capture.running())throw std::runtime_error("Audio device stopped; restart from the subtitle menu");
                if(input_.paused){last_packet=Clock::now();std::this_thread::sleep_for(std::chrono::milliseconds(20));continue;}
                size_t count=input_.ring.pop(audio.data(),audio.size());
                if(count)last_packet=Clock::now();
                else if(Clock::now()-last_packet>std::chrono::milliseconds(200)){
                    // Some loopback devices stop callbacks on silence; finalize the tail.
                    audio.fill(0);count=audio.size();last_packet=Clock::now();
                }
                if(count){
                    for(auto& event:stream.accept(audio.data(),count)){
                        std::lock_guard lock(mutex);auto old=jobs.drops;jobs.push(std::move(event));
                        if(jobs.drops!=old)on_status_("Overload: oldest pending sentence dropped");
                        cv.notify_one();
                    }
                }else std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }catch(...){finish();throw;}
        finish();
    }catch(const std::exception& e){on_status_(std::string("Error: ")+e.what());}
}
