#include "stream_settings.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
void Require(bool value, char const* message) {
    if (!value) throw std::runtime_error(message);
}
int main() {
    try {
        using namespace bridge;
        auto hd=OutputExtent(Resolution::P720), full=OutputExtent(Resolution::P1080);
        Require(hd.width==1280 && hd.height==720,"720p dimensions");
        Require(full.width==1920 && full.height==1080,"1080p dimensions");
        auto wide=Fit(3440,1440,hd);
        Require(wide.x==0 && wide.y>0 && wide.width==1280,"Ultrawide must letterbox");
        auto portrait=Fit(1080,1920,full);
        Require(portrait.x>0 && portrait.y==0 && portrait.height==1080,"Portrait must pillarbox");
        Require(std::abs(portrait.width/portrait.height-1080.f/1920)<1e-6f,"Keep source aspect ratio");
        auto native=Fit(3840,2160,full);
        Require(native.x==0 && native.y==0 && native.width==1920 && native.height==1080,"4K to 1080p");
        bool rejected=false;
        try { Fit(0,100,hd); } catch(std::invalid_argument const&) { rejected=true; }
        Require(rejected,"Reject empty source dimensions");
        using Time=FrameSchedule::Time;
        for (unsigned fps : {30u,60u}) {
            FrameSchedule schedule; schedule.Reset(Time(0),fps);
            auto initial=schedule.Deadline();
            auto frame=Time((1'000'000'000LL+fps-1)/fps);
            Require(initial==frame,"Correct frame interval");
            for(unsigned i=1;i<fps*10;++i) {
                auto wake=schedule.Deadline();
                schedule.Advance(wake+Time(1'000'000)); // 1 ms rendering cost
            }
            Require(schedule.Deadline()==frame*(fps*10),"Rendering must not cause timer drift");
            auto late=schedule.Deadline()+Time(1'000'000'000);
            schedule.Advance(late);
            Require(schedule.Deadline()==late+frame,"Missed frames must not accumulate");
            Require(schedule.Delay100ns(late)>0,"Timer delay must be positive");
            Require(schedule.Delay100ns(schedule.Deadline()+frame)==1,"Past deadline must wake immediately");
        }
        FrameSchedule invalid; rejected=false;
        try { invalid.Reset(Time(0),24); } catch(std::invalid_argument const&) { rejected=true; }
        Require(rejected,"Reject unsupported frame rate");
        std::cout << "PASS: resolution, aspect ratio, 30/60 fps deadlines, render-time drift, late frames\n";
        return 0;
    } catch(std::exception const& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
