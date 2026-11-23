#pragma once
#include <windows.h>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace bridge {
struct AudioDestination { std::wstring id,name; };
enum class AudioSource { Off, Application, System };
enum class AudioState { Stopped, Starting, Running, Failed };
class AudioBridge {
public:
    AudioBridge();
    ~AudioBridge();
    AudioBridge(AudioBridge const&)=delete;
    AudioBridge& operator=(AudioBridge const&)=delete;
    static std::vector<AudioDestination> Destinations();
    void Start(AudioSource source,DWORD process,std::wstring destination);
    void Stop();
    void Gain(float value) { gain_.store(value); }
    AudioState State() const { return state_.load(); }
    HRESULT Error() const { return error_.load(); }
    float Peak() const { return peak_.load(); }
    unsigned long long DroppedFrames() const { return dropped_.load(); }
private:
    void Run(AudioSource source,DWORD process,std::wstring const& destination) noexcept;
    HANDLE stop_{};
    std::thread worker_;
    std::atomic<AudioState> state_{AudioState::Stopped};
    std::atomic<HRESULT> error_{S_OK};
    std::atomic<float> peak_{0},gain_{1};
    std::atomic<unsigned long long> dropped_{0};
};
}
