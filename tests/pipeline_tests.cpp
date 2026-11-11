#include "audio.h"
#include "segmenter.h"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
void forced_boundary_preserves_every_sample(int vad_dip_frames=0){
    Segmenter segmenter;float frame[160];
    std::optional<Segment> first,next;
    for(int i=0;i<1350;++i){
        std::fill(std::begin(frame),std::end(frame),float(i+1)/5000.f);
        if(auto event=segmenter.feed(frame,(i>=1200&&i<1200+vad_dip_frames)?0.f:1.f)){
            if(event->final&&!first)first=std::move(event);
            else if(first&&event->id==first->id+1){next=std::move(event);break;}
        }
    }
    CHECK(first&&next);
    CHECK(std::abs(first->end-next->start)<1e-9);
    CHECK(next->audio.front()==1181.f/5000.f);
}
void forced_boundary_preserves_tail_when_speech_stops(){
    Segmenter segmenter;float frame[160];std::fill(std::begin(frame),std::end(frame),0.1f);
    std::vector<Segment> finals;
    for(int i=0;i<1200;++i)if(auto event=segmenter.feed(frame,1))
        if(event->final)finals.push_back(std::move(*event));
    std::fill(std::begin(frame),std::end(frame),0.f);
    for(int i=0;i<60;++i)if(auto event=segmenter.feed(frame,0))
        if(event->final)finals.push_back(std::move(*event));
    CHECK(finals.size()==2);CHECK(std::abs(finals.back().end-12.0)<1e-9);
    CHECK(std::abs(finals.front().end-finals.back().start)<1e-9);
}
void forced_boundary_reset_discards_carry(){
    Segmenter segmenter;float frame[160];std::fill(std::begin(frame),std::end(frame),0.1f);
    for(int i=0;i<1200;++i)segmenter.feed(frame,1);
    // Reset immediately after the forced final, before any following frames.
    segmenter.reset();bool saw_partial=false;
    std::fill(std::begin(frame),std::end(frame),0.125f);
    for(int i=0;i<110;++i)if(auto event=segmenter.feed(frame,1)){
        saw_partial=true;
        CHECK(std::all_of(event->audio.begin(),event->audio.end(),[](float x){return x==0.125f;}));
    }
    CHECK(saw_partial);
}
void long_speech_limits_repeated_work(){
    Segmenter segmenter;float frame[160];std::fill(std::begin(frame),std::end(frame),0.1f);
    int partials=0,finals=0;size_t repeated_samples=0;double first_partial=0;
    for(int i=0;i<1200;++i)if(auto event=segmenter.feed(frame,1)){
        if(event->final)++finals;
        else {if(!partials)first_partial=event->end;++partials;repeated_samples+=event->audio.size();}
    }
    CHECK(finals==1);CHECK(first_partial<=1.01);CHECK(partials>=4&&partials<=8);
    CHECK(repeated_samples<45*16000);
}
int main(){try{
    AudioRing<4> ring;float input[]={1,2,3,4},out[4];
    CHECK(ring.push(input,4));CHECK(!ring.push(input,1));CHECK(ring.pop(out,3)==3);CHECK(out[2]==3);
    CHECK(ring.push(input,3));CHECK(ring.pop(out,4)==4);CHECK(out[0]==4&&out[3]==3);CHECK(ring.available()==0);
    Segmenter s;float frame[160];std::fill(std::begin(frame),std::end(frame),0.1f);int finals=0,partials=0;uint64_t id=0;
    for(int i=0;i<300;++i){auto e=s.feed(frame,i<220?0.99f:0.0f);if(e){if(id)CHECK(e->id==id);id=e->id;if(e->final){++finals;CHECK(e->end<=2.21);}else ++partials;}}
    CHECK(finals==1);CHECK(partials>=2);CHECK(!s.flush());
    for(int i=0;i<100;++i)CHECK(!s.feed(frame,0));
    for(int i=0;i<100;++i)s.feed(frame,1);s.reset();CHECK(!s.flush());
    // A short fast word should activate VAD, and a forced boundary should retain
    // 200 ms of context for the following segment.
    Segmenter fast;std::optional<Segment> forced;int forced_count=0;
    for(int i=0;i<1205;++i)if(auto e=fast.feed(frame,1)){if(e->final){forced=std::move(e);++forced_count;}}
    CHECK(forced_count==1);CHECK(forced->audio.size()<=192000-3200);CHECK(forced->end<12.0);
    std::optional<Segment> next;
    for(int i=0;i<220;++i)if(auto e=fast.feed(frame,1)){if(!e->final){next=std::move(e);break;}}
    CHECK(next&&next->id==forced->id+1&&next->audio.size()>3200);
    std::optional<Segment> forced_again;
    for(int i=0;i<1250;++i)if(auto e=fast.feed(frame,1)){if(e->final){forced_again=std::move(e);break;}}
    CHECK(forced_again&&forced_again->id==forced->id+1&&forced_again->end<25.0);
    Segmenter short_word;for(int i=0;i<3;++i)CHECK(!short_word.feed(frame,1));
    bool short_final=false;for(int i=0;i<50;++i)if(auto e=short_word.feed(frame,0))short_final=e->final;
    CHECK(short_final);
    std::fill(std::begin(frame),std::end(frame),0.f);
    for(int i=0;i<150;++i)CHECK(!s.feed(frame,1));CHECK(!s.flush());
    SegmentQueue queue;queue.push({1,false,{}});queue.push({1,false,{1}});CHECK(queue.pop()->audio.size()==1);
    queue.push({1,false,{}});queue.push({1,true,{}});CHECK(queue.pop()->final);CHECK(queue.empty());
    for(int i=0;i<5;++i)queue.push({uint64_t(i),true,{}});CHECK(queue.drops==1);CHECK(queue.pop()->id==1);
    forced_boundary_preserves_every_sample();
    forced_boundary_preserves_every_sample(5);
    forced_boundary_preserves_tail_when_speech_stops();
    forced_boundary_reset_discards_carry();
    long_speech_limits_repeated_work();
    std::cout<<"PASS: ring overflow/wrap, endpoint/tail/reset, revision coalescing, bounded final queue\n";
}catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
