#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <stdexcept>

namespace bridge {
enum class Resolution { P720, P1080 };
struct Extent { unsigned width, height; };
constexpr Extent OutputExtent(Resolution resolution) {
    return resolution == Resolution::P1080 ? Extent{1920,1080} : Extent{1280,720};
}
struct Viewport { float x, y, width, height; };
inline Viewport Fit(unsigned width, unsigned height, Extent output) {
    if (!width || !height || !output.width || !output.height)
        throw std::invalid_argument("Empty video dimensions");
    float scale=std::min(static_cast<float>(output.width)/width,static_cast<float>(output.height)/height);
    float w=width*scale, h=height*scale;
    return {(output.width-w)/2,(output.height-h)/2,w,h};
}
// Absolute deadlines avoid adding render time to every frame interval. Late
// frames skip missed deadlines instead of queuing a burst of stale frames.
class FrameSchedule {
public:
    using Time = std::chrono::nanoseconds;
    void Reset(Time now, unsigned fps) {
        if (fps!=30 && fps!=60) throw std::invalid_argument("FPS must be 30 or 60");
        period_=Time((1'000'000'000LL+fps-1)/fps);
        next_=now+period_;
    }
    void Advance(Time now) {
        next_+=period_;
        if (next_<=now) next_=now+period_;
    }
    std::int64_t Delay100ns(Time now) const {
        auto ns=std::max<std::int64_t>(1,(next_-now).count());
        return (ns+99)/100;
    }
    Time Deadline() const { return next_; }
private:
    Time period_{33'333'334}, next_{};
};
}
