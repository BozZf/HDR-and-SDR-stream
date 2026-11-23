#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <stdexcept>

namespace bridge {
// Single audio-thread queue. Bounded in complete stereo frames, so overflow
// discards old audio instead of building unbounded streaming latency.
class AudioBuffer {
public:
    explicit AudioBuffer(size_t capacityFrames=12'000) : data_(capacityFrames*2) {
        if (!capacityFrames) throw std::invalid_argument("Empty audio queue");
    }
    void Clear() { head_=size_=0; }
    size_t Frames() const { return size_; }
    size_t Dropped() const { return dropped_; }
    float Push(std::int16_t const* samples,size_t frames,bool silent,float gain) {
        if (!samples && !silent && frames) throw std::invalid_argument("Missing audio samples");
        gain=std::isfinite(gain) ? std::clamp(gain,0.f,2.f) : 0.f;
        auto capacity=data_.size()/2;
        size_t skip=frames>capacity ? frames-capacity : 0;
        dropped_+=skip;
        size_t keep=frames-skip;
        if (size_+keep>capacity) {
            size_t discard=size_+keep-capacity;
            head_=(head_+discard)%capacity; size_-=discard; dropped_+=discard;
        }
        float peak=0;
        for (size_t frame=skip;frame<frames;++frame) {
            size_t offset=((head_+size_)%capacity)*2;
            for (size_t channel=0;channel<2;++channel) {
                int value=silent ? 0 : static_cast<int>(std::lround(samples[frame*2+channel]*gain));
                value=std::clamp(value,-32768,32767);
                data_[offset+channel]=static_cast<std::int16_t>(value);
                peak=std::max(peak,std::abs(static_cast<float>(value))/32768.f);
            }
            ++size_;
        }
        return peak;
    }
    size_t Pop(std::int16_t* destination,size_t frames) {
        if (!frames) return 0;
        if (!destination && frames) throw std::invalid_argument("Missing render buffer");
        size_t count=std::min(size_,frames),capacity=data_.size()/2;
        for (size_t frame=0;frame<count;++frame) {
            auto offset=((head_+frame)%capacity)*2;
            destination[frame*2]=data_[offset]; destination[frame*2+1]=data_[offset+1];
        }
        std::fill(destination+count*2,destination+frames*2,static_cast<std::int16_t>(0));
        head_=(head_+count)%capacity; size_-=count; return count;
    }
private:
    std::vector<std::int16_t> data_;
    size_t head_=0,size_=0,dropped_=0;
};
}
