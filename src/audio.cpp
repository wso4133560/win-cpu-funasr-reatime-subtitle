#include "audio.h"
#include <stdexcept>
#define MA_NO_ENGINE
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

struct LoopbackCapture::Impl {
    ma_context context{};
    ma_device device{};
    bool context_ready=false,device_ready=false;
    ~Impl(){if(device_ready)ma_device_uninit(&device);if(context_ready)ma_context_uninit(&context);}
};
LoopbackCapture::LoopbackCapture(AudioInput& sink):impl_(std::make_unique<Impl>()) {
    const ma_backend backend=ma_backend_wasapi;
    if(ma_context_init(&backend,1,nullptr,&impl_->context)!=MA_SUCCESS)
        throw std::runtime_error("WASAPI initialization failed");
    impl_->context_ready=true;
    auto config=ma_device_config_init(ma_device_type_loopback);
    config.capture.format=ma_format_f32;
    config.capture.channels=1;
    config.sampleRate=16000;
    config.periodSizeInMilliseconds=20;
    config.pUserData=&sink;
    config.dataCallback=[](ma_device* d,void*,const void* data,ma_uint32 frames){
        auto& s=*static_cast<AudioInput*>(d->pUserData);
        s.received.fetch_add(frames,std::memory_order_relaxed);
        if(!s.paused.load(std::memory_order_relaxed) && !s.ring.push(static_cast<const float*>(data),frames))
            s.dropped.fetch_add(frames,std::memory_order_relaxed);
    };
    config.notificationCallback=[](const ma_device_notification* n){
        if(n->type==ma_device_notification_type_rerouted)
            static_cast<AudioInput*>(n->pDevice->pUserData)->reroutes.fetch_add(1);
    };
    if(ma_device_init(&impl_->context,&config,&impl_->device)!=MA_SUCCESS)
        throw std::runtime_error("Cannot open default Windows output device in loopback mode");
    impl_->device_ready=true;
}
LoopbackCapture::~LoopbackCapture()=default;
void LoopbackCapture::start(){if(ma_device_start(&impl_->device)!=MA_SUCCESS)throw std::runtime_error("WASAPI capture start failed");}
bool LoopbackCapture::running() const {return ma_device_is_started(&impl_->device)==MA_TRUE;}
std::string LoopbackCapture::device_name() const {return impl_->device.capture.name;}
std::vector<float> load_audio(const std::string& path) {
    auto config=ma_decoder_config_init(ma_format_f32,1,16000);
    ma_decoder d{};
    if(ma_decoder_init_file(path.c_str(),&config,&d)!=MA_SUCCESS) throw std::runtime_error("Cannot decode input audio");
    std::vector<float> result;
    std::array<float,4096> buf{};
    for(;;){ma_uint64 count=0;auto r=ma_decoder_read_pcm_frames(&d,buf.data(),buf.size(),&count);
        result.insert(result.end(),buf.begin(),buf.begin()+count);
        if(r!=MA_SUCCESS||count==0)break;}
    ma_decoder_uninit(&d);
    return result;
}
void save_audio(const std::string& path,const std::vector<float>& samples){
    auto config=ma_encoder_config_init(ma_encoding_format_wav,ma_format_f32,1,16000);
    ma_encoder e{};
    if(ma_encoder_init_file(path.c_str(),&config,&e)!=MA_SUCCESS)throw std::runtime_error("Cannot save capture");
    ma_uint64 written=0;
    auto result=ma_encoder_write_pcm_frames(&e,samples.data(),samples.size(),&written);
    ma_encoder_uninit(&e);
    if(result!=MA_SUCCESS||written!=samples.size())throw std::runtime_error("Incomplete WAV write");
}
