#include "audio.h"
#include "segmenter.h"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(){
    AudioRing<4> ring;float input[]={1,2,3,4},out[4];
    CHECK(ring.push(input,4));CHECK(!ring.push(input,1));CHECK(ring.pop(out,3)==3);CHECK(out[2]==3);
    CHECK(ring.push(input,3));CHECK(ring.pop(out,4)==4);CHECK(out[0]==4&&out[3]==3);CHECK(ring.available()==0);
    Segmenter s;float frame[160];std::fill(std::begin(frame),std::end(frame),0.1f);int finals=0,partials=0;uint64_t id=0;
    for(int i=0;i<300;++i){auto e=s.feed(frame,i<220?0.99f:0.0f);if(e){if(id)CHECK(e->id==id);id=e->id;if(e->final){++finals;CHECK(e->end<=2.21);}else ++partials;}}
    CHECK(finals==1);CHECK(partials>=2);CHECK(!s.flush());
    for(int i=0;i<100;++i)CHECK(!s.feed(frame,0));
    for(int i=0;i<100;++i)s.feed(frame,1);s.reset();CHECK(!s.flush());
    std::fill(std::begin(frame),std::end(frame),0.f);
    for(int i=0;i<150;++i)CHECK(!s.feed(frame,1));CHECK(!s.flush());
    SegmentQueue queue;queue.push({1,false,{}});queue.push({1,false,{1}});CHECK(queue.pop()->audio.size()==1);
    queue.push({1,false,{}});queue.push({1,true,{}});CHECK(queue.pop()->final);CHECK(queue.empty());
    for(int i=0;i<5;++i)queue.push({uint64_t(i),true,{}});CHECK(queue.drops==1);CHECK(queue.pop()->id==1);
    std::cout<<"PASS: ring overflow/wrap, endpoint/tail/reset, revision coalescing, bounded final queue\n";
}
