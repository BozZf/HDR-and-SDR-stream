#include "audio_buffer.h"
#include <array>
#include <iostream>
void Require(bool value,char const* detail) { if (!value) throw std::runtime_error(detail); }
int main() {
    try {
        bridge::AudioBuffer queue(3);
        std::array<std::int16_t,8> in{100,-100,200,-200,300,-300,400,-400},out{};
        queue.Push(in.data(),2,false,1);
        Require(queue.Pop(out.data(),1)==1 && out[0]==100 && out[1]==-100,"Keep stereo alignment");
        queue.Push(in.data()+4,2,false,1);
        queue.Pop(out.data(),4);
        Require(out==std::array<std::int16_t,8>{200,-200,300,-300,400,-400,0,0},"Wrap and pad underrun with silence");
        queue.Push(in.data(),4,false,1); queue.Pop(out.data(),3);
        Require(out[0]==200 && out[4]==400 && queue.Dropped()==1,"Overflow keeps freshest frames");
        queue.Push(in.data(),3,false,1); queue.Push(in.data()+6,1,false,1);
        queue.Pop(out.data(),3);
        Require(out[0]==200 && out[4]==400 && queue.Dropped()==2,"Full queue drops old audio");
        std::array<std::int16_t,2> loud{30000,-30000};
        Require(queue.Push(loud.data(),1,false,2)==1,"Peak reflects clipping");
        queue.Pop(out.data(),1); Require(out[0]==32767 && out[1]==-32768,"Gain saturates without wrapping");
        queue.Push(nullptr,2,true,1); queue.Pop(out.data(),2);
        Require(out[0]==0 && out[3]==0,"Silent WASAPI packets accept a null pointer");
        queue.Push(in.data(),1,false,0); queue.Pop(out.data(),1);
        Require(out[0]==0 && out[1]==0,"Mute gain emits zero");
        queue.Push(in.data(),1,false,1); queue.Clear();
        Require(queue.Frames()==0,"Stop/discontinuity clears queued samples");
        bool invalid=false;
        try { queue.Push(nullptr,1,false,1); } catch(std::invalid_argument const&) { invalid=true; }
        Require(invalid,"Reject malformed packet");
        std::cout<<"PASS: stereo queue, silence, gain, clipping, bounded latency, underrun, clear\n"; return 0;
    } catch(std::exception const& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
