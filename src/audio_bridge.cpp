#include "audio_bridge.h"
#include "audio_buffer.h"
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <winrt/base.h>
#include <memory>

using winrt::com_ptr;
using winrt::check_hresult;
namespace bridge {
namespace {
struct Handle {
    HANDLE value{};
    explicit Handle(bool manual=false) : value(CreateEventW(nullptr,manual,FALSE,nullptr)) { if (!value) winrt::throw_last_error(); }
    ~Handle() { CloseHandle(value); }
};
struct Activation : winrt::implements<Activation,IActivateAudioInterfaceCompletionHandler> {
    Handle ready{true};
    HRESULT result=E_PENDING;
    com_ptr<IAudioClient> client;
    AUDIOCLIENT_ACTIVATION_PARAMS parameters{};
    PROPVARIANT variant{};
    HRESULT __stdcall ActivateCompleted(IActivateAudioInterfaceAsyncOperation* operation) noexcept final {
        try {
            HRESULT activated=E_FAIL;
            com_ptr<IUnknown> unknown;
            check_hresult(operation->GetActivateResult(&activated,unknown.put()));
            check_hresult(activated);
            client=unknown.as<IAudioClient>(); result=S_OK;
        } catch (...) { result=winrt::to_hresult(); }
        SetEvent(ready.value); return S_OK;
    }
};
struct ClientStop {
    IAudioClient* value;
    ~ClientStop() { if (value) value->Stop(); }
};
com_ptr<IMMDeviceEnumerator> Enumerator() {
    com_ptr<IMMDeviceEnumerator> value;
    check_hresult(CoCreateInstance(__uuidof(MMDeviceEnumerator),nullptr,CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),value.put_void()));
    return value;
}
WAVEFORMATEX Format() {
    WAVEFORMATEX format{};
    format.wFormatTag=WAVE_FORMAT_PCM; format.nChannels=2; format.nSamplesPerSec=48'000;
    format.wBitsPerSample=16; format.nBlockAlign=4; format.nAvgBytesPerSec=192'000;
    return format;
}
}
AudioBridge::AudioBridge() {
    stop_=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (!stop_) winrt::throw_last_error();
}
AudioBridge::~AudioBridge() { Stop(); CloseHandle(stop_); }
std::vector<AudioDestination> AudioBridge::Destinations() {
    std::vector<AudioDestination> result;
    auto enumerator=Enumerator();
    com_ptr<IMMDeviceCollection> collection;
    check_hresult(enumerator->EnumAudioEndpoints(eRender,DEVICE_STATE_ACTIVE,collection.put()));
    UINT count=0; check_hresult(collection->GetCount(&count));
    for (UINT i=0;i<count;++i) {
        com_ptr<IMMDevice> device; check_hresult(collection->Item(i,device.put()));
        LPWSTR raw=nullptr; check_hresult(device->GetId(&raw));
        std::wstring id(raw); CoTaskMemFree(raw);
        com_ptr<IPropertyStore> properties; check_hresult(device->OpenPropertyStore(STGM_READ,properties.put()));
        PROPVARIANT name{};
        check_hresult(properties->GetValue(PKEY_Device_FriendlyName,&name));
        std::wstring friendly=name.vt==VT_LPWSTR && name.pwszVal ? name.pwszVal : id;
        PropVariantClear(&name); result.push_back({id,friendly});
    }
    return result;
}
void AudioBridge::Start(AudioSource source,DWORD process,std::wstring destination) {
    Stop();
    if (source==AudioSource::Off) return;
    if (destination.empty() || (source==AudioSource::Application && !process))
        throw winrt::hresult_error(E_INVALIDARG,L"Selecione a origem e o destino do áudio.");
    ResetEvent(stop_); error_=S_OK; peak_=0; dropped_=0; state_=AudioState::Starting;
    worker_=std::thread([this,source,process,destination=std::move(destination)] { Run(source,process,destination); });
}
void AudioBridge::Stop() {
    SetEvent(stop_);
    if (worker_.joinable()) worker_.join();
    state_=AudioState::Stopped; peak_=0;
}
void AudioBridge::Run(AudioSource source,DWORD process,std::wstring const& destination) noexcept {
    HRESULT apartment=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if (FAILED(apartment)) { error_=apartment; state_=AudioState::Failed; return; }
    try {
        // Process-loopback is independent of physical render endpoints. Exclude
        // this process for system capture so forwarded audio cannot feed itself.
        auto activation=winrt::make_self<Activation>();
        activation->parameters.ActivationType=AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
        activation->parameters.ProcessLoopbackParams.TargetProcessId=
            source==AudioSource::Application ? process : GetCurrentProcessId();
        activation->parameters.ProcessLoopbackParams.ProcessLoopbackMode=source==AudioSource::Application
            ? PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE : PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE;
        activation->variant.vt=VT_BLOB;
        activation->variant.blob.cbSize=sizeof(activation->parameters);
        activation->variant.blob.pBlobData=reinterpret_cast<BYTE*>(&activation->parameters);
        com_ptr<IActivateAudioInterfaceAsyncOperation> operation;
        check_hresult(ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,__uuidof(IAudioClient),
            &activation->variant,activation.get(),operation.put()));
        HANDLE activateEvents[]={stop_,activation->ready.value};
        auto wait=WaitForMultipleObjects(2,activateEvents,FALSE,10'000);
        if (wait==WAIT_OBJECT_0) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_CANCELLED));
        if (wait!=WAIT_OBJECT_0+1) throw winrt::hresult_error(HRESULT_FROM_WIN32(ERROR_TIMEOUT),L"A ativação de áudio não respondeu.");
        check_hresult(activation->result);
        auto capture=activation->client;
        auto enumerator=Enumerator();
        com_ptr<IMMDevice> destinationDevice;
        check_hresult(enumerator->GetDevice(destination.c_str(),destinationDevice.put()));
        com_ptr<IAudioClient> render;
        check_hresult(destinationDevice->Activate(__uuidof(IAudioClient),CLSCTX_ALL,nullptr,render.put_void()));
        auto format=Format();
        Handle captureEvent,renderEvent;
        check_hresult(capture->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK|AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
            200'000,0,&format,nullptr));
        check_hresult(render->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK|AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
            500'000,0,&format,nullptr));
        check_hresult(capture->SetEventHandle(captureEvent.value));
        check_hresult(render->SetEventHandle(renderEvent.value));
        com_ptr<IAudioCaptureClient> reader;
        com_ptr<IAudioRenderClient> writer;
        check_hresult(capture->GetService(__uuidof(IAudioCaptureClient),reader.put_void()));
        check_hresult(render->GetService(__uuidof(IAudioRenderClient),writer.put_void()));
        UINT capacity=0; check_hresult(render->GetBufferSize(&capacity));
        BYTE* initial=nullptr; check_hresult(writer->GetBuffer(capacity,&initial));
        check_hresult(writer->ReleaseBuffer(capacity,AUDCLNT_BUFFERFLAGS_SILENT));
        check_hresult(render->Start()); ClientStop stopRender{render.get()};
        check_hresult(capture->Start()); ClientStop stopCapture{capture.get()};
        AudioBuffer queue;
        state_=AudioState::Running;
        HANDLE events[]={stop_,captureEvent.value,renderEvent.value};
        for (;;) {
            auto ready=WaitForMultipleObjects(3,events,FALSE,100);
            if (ready==WAIT_OBJECT_0) break;
            if (ready==WAIT_FAILED) winrt::throw_last_error();
            UINT available=0; check_hresult(reader->GetNextPacketSize(&available));
            float currentPeak=0;
            while (available) {
                BYTE* bytes=nullptr; UINT frames=0; DWORD flags=0;
                check_hresult(reader->GetBuffer(&bytes,&frames,&flags,nullptr,nullptr));
                if (flags&AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) queue.Clear();
                currentPeak=std::max(currentPeak,queue.Push(reinterpret_cast<std::int16_t const*>(bytes),
                    frames,(flags&AUDCLNT_BUFFERFLAGS_SILENT)!=0,gain_.load()));
                check_hresult(reader->ReleaseBuffer(frames));
                check_hresult(reader->GetNextPacketSize(&available));
            }
            peak_=std::max(currentPeak,peak_.load()*0.85f); dropped_=queue.Dropped();
            UINT padding=0; check_hresult(render->GetCurrentPadding(&padding));
            if (padding>capacity) throw winrt::hresult_error(E_UNEXPECTED);
            UINT free=capacity-padding;
            if (free) {
                BYTE* bytes=nullptr; check_hresult(writer->GetBuffer(free,&bytes));
                auto copied=queue.Pop(reinterpret_cast<std::int16_t*>(bytes),free);
                check_hresult(writer->ReleaseBuffer(free,copied ? 0 : AUDCLNT_BUFFERFLAGS_SILENT));
            }
        }
    } catch (...) {
        if (WaitForSingleObject(stop_,0)==WAIT_OBJECT_0) state_=AudioState::Stopped;
        else { error_=winrt::to_hresult(); state_=AudioState::Failed; }
    }
    // All WASAPI interfaces are released in the worker's apartment first.
    CoUninitialize();
}
}
