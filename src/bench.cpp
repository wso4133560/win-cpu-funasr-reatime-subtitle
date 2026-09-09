#include "pipeline.h"
#include <windows.h>
#include <mmsystem.h>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <stdexcept>
using Clock=std::chrono::steady_clock;
int main(int argc,char** argv){
    try{
        std::string model="models/sensevoice-small-q8.gguf",vad="models/fsmn-vad.gguf",audio,out="test-output";
        int threads=8,live=0,capture_seconds=0;bool stream=false;
        for(int i=1;i<argc;++i){std::string a=argv[i];
            auto value=[&]()->std::string{if(++i>=argc)throw std::runtime_error("Missing argument value");return argv[i];};
            if(a=="--model")model=value();else if(a=="--vad")vad=value();else if(a=="--audio")audio=value();
            else if(a=="--threads")threads=std::stoi(value());else if(a=="--live")live=std::stoi(value());
            else if(a=="--capture")capture_seconds=std::stoi(value());else if(a=="--stream")stream=true;
            else if(a=="--out")out=value();else throw std::runtime_error("Unknown option: "+a);
        }
        if(threads<1||threads>32)throw std::runtime_error("threads must be 1..32");
        std::filesystem::create_directories(out);
        if(live>0){
            std::mutex mutex;std::ofstream final(out+"/live-final.txt");
            auto start=Clock::now();
            Pipeline pipeline({model,vad,out,threads},[&](SubtitleEvent e){
                std::lock_guard lock(mutex);
                std::cout<<"event "<<e.id<<" final="<<e.final<<" wall_s="<<std::chrono::duration<double>(Clock::now()-start).count()<<" compute_ms="<<e.compute_ms<<" text="<<e.text<<std::endl;
                if(e.final){final<<e.text<<'\n';final.flush();}
            },[&](std::string s){std::lock_guard lock(mutex);std::cout<<"status "<<s<<std::endl;});
            pipeline.start();std::this_thread::sleep_for(std::chrono::seconds(2));
            if(!audio.empty()&&!PlaySoundA(audio.c_str(),nullptr,SND_FILENAME|SND_ASYNC|SND_NODEFAULT))throw std::runtime_error("Cannot play test WAV");
            std::this_thread::sleep_for(std::chrono::seconds(live));PlaySoundW(nullptr,nullptr,0);return 0;
        }
        if(capture_seconds>0){
            AudioInput sink;LoopbackCapture capture(sink);capture.start();
            std::cout<<"capture_device="<<capture.device_name()<<std::endl;
            if(!audio.empty())PlaySoundA(audio.c_str(),nullptr,SND_FILENAME|SND_ASYNC|SND_NODEFAULT);
            std::vector<float> samples;std::array<float,2560> block;auto begin=Clock::now();
            while(Clock::now()-begin<std::chrono::seconds(capture_seconds)){
                size_t n=sink.ring.pop(block.data(),block.size());samples.insert(samples.end(),block.begin(),block.begin()+n);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            PlaySoundW(nullptr,nullptr,0);save_audio(out+"/loopback.wav",samples);
            std::cout<<"captured_samples="<<samples.size()<<" dropped="<<sink.dropped.load()<<std::endl;return 0;
        }
        if(audio.empty())throw std::runtime_error("--audio FILE is required");
        auto wav=load_audio(audio);auto before=Clock::now();SenseVoice engine(model,threads);
        std::cout<<"load_ms="<<std::chrono::duration<double,std::milli>(Clock::now()-before).count()<<" audio_s="<<wav.size()/16000.0<<std::endl;
        std::ofstream transcript(out+"/hypothesis.txt");
        if(stream){
            SpeechVad v(vad,2);StreamingVad s(v);
            wav.resize(wav.size()+16000,0);
            for(size_t p=0;p<wav.size();p+=2560){
                for(auto& e:s.accept(wav.data()+p,std::min<size_t>(2560,wav.size()-p))){
                    auto t=Clock::now();auto text=engine.transcribe(e.audio);
                    std::cout<<"id="<<e.id<<" final="<<e.final<<" end="<<e.end<<" compute_ms="<<std::chrono::duration<double,std::milli>(Clock::now()-t).count()<<" text="<<text<<std::endl;
                    if(e.final)transcript<<text<<'\n';
                }
            }
        }else for(int i=0;i<3;++i){
            before=Clock::now();auto text=engine.transcribe(wav);
            std::cout<<"run="<<i<<" compute_ms="<<std::chrono::duration<double,std::milli>(Clock::now()-before).count()<<" text="<<text<<std::endl;
            if(i==0)transcript<<text<<'\n';
        }
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
