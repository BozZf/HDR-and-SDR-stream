#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <d3d11.h>
#include <dwmapi.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <algorithm>
#include <string>
#include <cstring>
#include <cwchar>
#include <vector>
#include <chrono>
#include <charconv>
#include <span>
#include "stream_settings.h"
#include "video_frame.h"
#include "audio_bridge.h"
#include "sdr_readback.h"
#include "shader_source.h"
#include "web_ui.h"
#include "native_theme.h"

#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

using namespace winrt;
using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;
constexpr UINT kClosed = WM_APP + 1, kTray = WM_APP + 2, kWebCommand = WM_APP + 3;
constexpr UINT_PTR kCloseDialogTimer=1, kWebStartupTimer=2;
constexpr UINT kSwitchUi=0x1f00;
constexpr int kPick = 100, kStop = 101, kMode = 102, kExposure = 103,
              kWhite = 104, kRate = 105, kShow = 106, kExit = 107,
              kSourceKind = 108, kSource = 109, kRefresh = 110, kResolution = 111, kHideOutput = 112,
              kAudio = 113, kAudioMode = 114, kAudioDestination = 115,
              kAudioRefresh = 116, kAudioGain = 117, kAudioApply = 118, kLowerOutput = 119,
              kLowerToolbar = 120, kResetClose = 121, kTheme = 122;

enum class CloseChoice : DWORD { Ask, Minimize, Exit };
constexpr wchar_t kSettingsKey[]=L"Software\\HDR Bridge";

DWORD ReadSetting(wchar_t const* name,DWORD maximum) {
    DWORD value=0,size=sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER,kSettingsKey,name,RRF_RT_REG_DWORD,nullptr,&value,&size)==ERROR_SUCCESS &&
        value<=maximum) return value;
    return 0;
}

bool SaveSetting(wchar_t const* name,DWORD value) {
    HKEY key{};
    if (RegCreateKeyExW(HKEY_CURRENT_USER,kSettingsKey,0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)
        return false;
    auto result=RegSetValueExW(key,name,0,REG_DWORD,reinterpret_cast<BYTE const*>(&value),sizeof(value));
    RegCloseKey(key);
    return result==ERROR_SUCCESS;
}

bool ParseValues(std::string_view input,std::span<int> values) {
    for (auto& value : values) {
        if (input.empty()) return false;
        auto end=input.data()+input.size();
        auto result=std::from_chars(input.data(),end,value);
        if (result.ec!=std::errc{}) return false;
        bool last=&value==&values.back();
        if (last) return result.ptr==end;
        if (result.ptr==end || *result.ptr!=':') return false;
        input.remove_prefix(static_cast<size_t>(result.ptr-input.data())+1);
    }
    return input.empty();
}

com_ptr<ID3DBlob> Compile(char const* entry, char const* target) {
    com_ptr<ID3DBlob> blob, errors;
    HRESULT hr = D3DCompile(kColorShader, sizeof(kColorShader)-1, nullptr, nullptr,
        nullptr, entry, target, D3DCOMPILE_ENABLE_STRICTNESS, 0, blob.put(), errors.put());
    if (FAILED(hr)) {
        std::string detail = errors ? std::string(static_cast<char*>(errors->GetBufferPointer()), errors->GetBufferSize()) : "Shader compilation failed";
        throw hresult_error(hr, to_hstring(detail));
    }
    return blob;
}

struct App {
    HWND controls{}, output{}, outputToolbar{}, preview{}, modeBox{}, rateBox{}, exposureSlider{}, whiteSlider{},
         exposureLabel{}, whiteLabel{}, status{}, sourceKindBox{}, sourceBox{}, resolutionBox{};
    bool hdr = false, running = false, gdiOutput = false;
    bool outputLowered = false;
    bridge::WebUi web;
    bridge::NativeTheme theme;
    HWND webHost{},themeBox{};
    bool webActive=false, webPresenting=false;
    std::wstring videoStatusText=L"Parado. Escolha a janela ou monitor de origem.", audioStatusText;
    CloseChoice closeChoice=CloseChoice::Ask;
    bool confirmingClose=false;
    HWND outputToolbarTitle{},outputToolbarLower{},outputToolbarClose{};
    HWND audioWindow{},audioModeBox{},audioDestinationBox{},audioGainSlider{},audioGainLabel{},audioStatus{},audioSummary{};
    bridge::AudioBridge audio;
    bridge::AudioSource audioSource=bridge::AudioSource::Off;
    std::vector<bridge::AudioDestination> audioDestinations;
    std::wstring audioDestination,audioConfigError;
    DWORD activeProcess=0;
    HWND activeSourceWindow{};
    bridge::SdrReadback compatibility;
    void RefreshAudioDestinations() {
        std::wstring previous=audioDestination;
        auto selection=SendMessageW(audioDestinationBox,CB_GETCURSEL,0,0);
        if (selection>0 && static_cast<size_t>(selection)<=audioDestinations.size())
            previous=audioDestinations[static_cast<size_t>(selection)-1].id;
        audioDestinations=bridge::AudioBridge::Destinations();
        SendMessageW(audioDestinationBox,CB_RESETCONTENT,0,0);
        SendMessageW(audioDestinationBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Selecione um destino virtual (nenhum por padrão)"));
        size_t selected=0;
        for (size_t i=0;i<audioDestinations.size();++i) {
            SendMessageW(audioDestinationBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(audioDestinations[i].name.c_str()));
            if (audioDestinations[i].id==previous) selected=i+1;
        }
        SendMessageW(audioDestinationBox,CB_SETCURSEL,selected,0);
    }
    void RestartAudio() {
        audio.Stop(); audioConfigError.clear();
        if (!running || audioSource==bridge::AudioSource::Off) { RefreshAudioStatus(); return; }
        if (audioDestination.empty()) audioConfigError=L"Selecione o destino virtual em Áudio.";
        else if (audioSource==bridge::AudioSource::Application && !activeProcess)
            audioConfigError=L"Áudio do aplicativo exige capturar uma janela.";
        if (audioConfigError.empty()) {
            try { audio.Start(audioSource,activeProcess,audioDestination); }
            catch (hresult_error const& e) { audioConfigError=e.message().c_str(); }
        }
        RefreshAudioStatus();
    }
    void RefreshAudioStatus() {
        std::wstring text;
        if (!audioConfigError.empty()) text=L"Áudio: "+audioConfigError;
        else switch(audio.State()) {
        case bridge::AudioState::Stopped: text=audioSource==bridge::AudioSource::Off ? L"Áudio desligado" : L"Áudio configurado; inicie a captura."; break;
        case bridge::AudioState::Starting: text=L"Áudio: iniciando captura e destino..."; break;
        case bridge::AudioState::Running: text=L"Áudio: nível "+std::to_wstring(static_cast<int>(audio.Peak()*100))+L"% · destino ativo"; break;
        case bridge::AudioState::Failed: {
            wchar_t code[32]; swprintf_s(code,L"0x%08X",static_cast<unsigned>(audio.Error()));
            text=L"Áudio falhou ("+std::wstring(code)+L"). Reabra Áudio e aplique novamente."; break;
        }
        }
        if (audioStatusText!=text) {
            audioStatusText=text;
            if (audioSummary) SetWindowTextW(audioSummary,text.c_str());
            if (audioStatus) SetWindowTextW(audioStatus,text.c_str());
            if (webActive) SendText(L"audio-status",text);
        }
    }
    void ApplyAudio() {
        auto selection=SendMessageW(audioModeBox,CB_GETCURSEL,0,0);
        auto destination=SendMessageW(audioDestinationBox,CB_GETCURSEL,0,0);
        auto next=selection==1 ? bridge::AudioSource::Application : selection==2 ? bridge::AudioSource::System : bridge::AudioSource::Off;
        if (next!=bridge::AudioSource::Off && (destination<=0 || static_cast<size_t>(destination)>audioDestinations.size())) {
            MessageBoxW(audioWindow,L"Escolha um dispositivo de reprodução virtual como destino. No serviço, selecione a entrada correspondente. Nenhum dispositivo é instalado pelo app.",L"Destino do áudio",MB_ICONINFORMATION);
            return;
        }
        audioSource=next;
        audioDestination=destination>0 && static_cast<size_t>(destination)<=audioDestinations.size()
            ? audioDestinations[static_cast<size_t>(destination)-1].id : L"";
        audio.Gain(static_cast<float>(SendMessageW(audioGainSlider,TBM_GETPOS,0,0))/100);
        RestartAudio();
    }
    void AudioGain() {
        auto value=SendMessageW(audioGainSlider,TBM_GETPOS,0,0);
        audio.Gain(static_cast<float>(value)/100);
        auto label=L"Volume: "+std::to_wstring(value)+L"%";
        SetWindowTextW(audioGainLabel,label.c_str());
    }
    void OpenAudio(bool show=true) {
        if (!audioWindow) {
            audioWindow=CreateWindowW(L"HdrBridgeWindow",L"HDR Bridge — Áudio",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_CLIPCHILDREN,
                CW_USEDEFAULT,CW_USEDEFAULT,560,470,controls,nullptr,GetModuleHandleW(nullptr),nullptr);
            if (!audioWindow) throw_last_error();
            SetWindowDisplayAffinity(audioWindow,WDA_EXCLUDEFROMCAPTURE);
            Child(L"STATIC",L"Origem de áudio",0,20,20,150,22,0,audioWindow);
            audioModeBox=Child(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,20,48,500,130,kAudioMode,audioWindow);
            SendMessageW(audioModeBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Desligado"));
            SendMessageW(audioModeBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Aplicativo da janela capturada (inclui filhos)"));
            SendMessageW(audioModeBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Sistema (exceto o próprio HDR Bridge)"));
            SendMessageW(audioModeBox,CB_SETCURSEL,0,0);
            Child(L"STATIC",L"Destino: saída de reprodução do seu cabo virtual",0,20,94,500,22,0,audioWindow);
            audioDestinationBox=Child(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,20,120,500,180,kAudioDestination,audioWindow);
            audioGainLabel=Child(L"STATIC",L"Volume: 100%",0,20,166,150,22,0,audioWindow);
            audioGainSlider=Child(TRACKBAR_CLASSW,L"",TBS_AUTOTICKS|WS_TABSTOP,180,158,340,34,kAudioGain,audioWindow);
            SendMessageW(audioGainSlider,TBM_SETRANGE,TRUE,MAKELPARAM(0,200));
            SendMessageW(audioGainSlider,TBM_SETPOS,TRUE,100);
            Child(L"STATIC",L"No serviço, escolha a entrada de gravação correspondente ao cabo.\r\nNão escolha seus alto-falantes como destino: pode duplicar o som.\r\nSistema inclui notificações e chamadas; prefira só o aplicativo.",0,20,206,500,62,0,audioWindow);
            audioStatus=Child(L"STATIC",L"Áudio desligado",0,20,280,500,60,0,audioWindow);
            Child(L"BUTTON",L"Atualizar dispositivos",WS_TABSTOP,20,360,230,32,kAudioRefresh,audioWindow);
            Child(L"BUTTON",L"Aplicar áudio",WS_TABSTOP,280,360,240,32,kAudioApply,audioWindow);
            RefreshAudioDestinations();
            theme.Refresh(audioWindow);
        }
        RefreshAudioStatus();
        if (show) { ShowWindow(audioWindow,SW_SHOW); SetForegroundWindow(audioWindow); }
    }
    void DrawGdiOutput(HDC dc) {
        RECT client{}; GetClientRect(output,&client);
        compatibility.Draw(dc,client);
    }
    void PaintOutput() {
        PAINTSTRUCT paint{};
        BeginPaint(output,&paint);
        DrawGdiOutput(paint.hdc);
        EndPaint(output,&paint);
    }
    struct Source { HMONITOR monitor{}; HWND window{}; DWORD process{}; std::wstring label; };
    std::vector<Source> sources;
    bridge::Resolution resolution = bridge::Resolution::P720;
    bridge::FrameSchedule schedule;
    HANDLE frameTimer{};
    static bridge::FrameSchedule::Time Now() {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
    }
    void ArmTimer(bool reset = false) {
        if (!running) return;
        if (reset) schedule.Reset(Now(), fps);
        LARGE_INTEGER due{}; due.QuadPart = -schedule.Delay100ns(Now());
        if (!SetWaitableTimer(frameTimer, &due, 0, nullptr, nullptr, FALSE)) throw_last_error();
    }
    void OnFrameTimer() {
        RefreshAudioStatus();
        UpdateOutputToolbar();
        Tick();
        if (running) { schedule.Advance(Now()); ArmTimer(); }
    }
    void FitOutputWindow() {
        if (IsIconic(output)) return;
        auto extent=bridge::OutputExtent(resolution);
        RECT current{};
        if (!GetWindowRect(output,&current)) throw_last_error();
        MONITORINFO info{}; info.cbSize=sizeof(info);
        if (!GetMonitorInfoW(MonitorFromWindow(output,MONITOR_DEFAULTTONEAREST),&info)) throw_last_error();
        auto width=static_cast<LONG>(extent.width),height=static_cast<LONG>(extent.height);
        auto const& workArea=info.rcWork;
        auto x=std::clamp(current.left,workArea.left,std::max(workArea.left,workArea.right-width));
        auto y=std::clamp(current.top,workArea.top,std::max(workArea.top,workArea.bottom-height));
        if (current.left==x && current.top==y && current.right-current.left==width && current.bottom-current.top==height)
            return;
        if (!SetWindowPos(output,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE)) throw_last_error();
    }
    void HideOutputToolbar() {
        if (outputToolbar && IsWindowVisible(outputToolbar)) ShowWindow(outputToolbar,SW_HIDE);
    }
    void UpdateOutputToolbar() {
        if (!outputToolbar) return;
        if (!running || outputLowered || !IsWindowVisible(output) || IsIconic(output)) {
            HideOutputToolbar();
            return;
        }
        RECT outputBounds{};
        POINT pointer{};
        if (!GetWindowRect(output,&outputBounds) || !GetCursorPos(&pointer)) {
            HideOutputToolbar();
            return;
        }
        auto dpi=static_cast<int>(GetDpiForWindow(output));
        auto px=[dpi](int value) { return MulDiv(value,dpi,96); };
        auto width=px(310),height=px(38);
        auto x=outputBounds.right-width,y=outputBounds.top;
        RECT hotspot{x,y,outputBounds.right,y+px(12)};
        RECT toolbarBounds{x,y,outputBounds.right,y+height};
        auto hit=GetAncestor(WindowFromPoint(pointer),GA_ROOT);
        bool insideToolbar=IsWindowVisible(outputToolbar) && hit==outputToolbar && PtInRect(&toolbarBounds,pointer);
        bool insideHotspot=hit==output && PtInRect(&hotspot,pointer);
        if (!insideToolbar && !insideHotspot) {
            HideOutputToolbar();
            return;
        }
        RECT current{};
        if (IsWindowVisible(outputToolbar) && GetWindowRect(outputToolbar,&current) && EqualRect(&current,&toolbarBounds)) return;
        SetWindowPos(outputToolbar,HWND_TOP,x,y,width,height,SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_SHOWWINDOW);
        MoveWindow(outputToolbarTitle,px(10),px(10),px(120),px(22),TRUE);
        MoveWindow(outputToolbarLower,px(133),px(5),px(100),px(28),TRUE);
        MoveWindow(outputToolbarClose,px(238),px(5),px(65),px(28),TRUE);
    }
    static BOOL CALLBACK AddMonitor(HMONITOR monitor,HDC,LPRECT,LPARAM data) {
        auto& self = *reinterpret_cast<App*>(data);
        MONITORINFOEXW info{}; info.cbSize=sizeof(info);
        if (GetMonitorInfoW(monitor,&info)) {
            std::wstring label=info.szDevice;
            label+=L" — "+std::to_wstring(info.rcMonitor.right-info.rcMonitor.left)+L" × "+
                std::to_wstring(info.rcMonitor.bottom-info.rcMonitor.top);
            if (info.dwFlags & MONITORINFOF_PRIMARY) label+=L" (principal)";
            self.sources.push_back({monitor,nullptr,0,label});
        }
        return TRUE;
    }
    static BOOL CALLBACK AddWindow(HWND window,LPARAM data) {
        auto& self = *reinterpret_cast<App*>(data);
        if (!IsWindowVisible(window) || GetAncestor(window,GA_ROOT)!=window) return TRUE;
        DWORD process=0; GetWindowThreadProcessId(window,&process);
        if (process==GetCurrentProcessId()) return TRUE;
        if (GetWindowLongPtrW(window,GWL_EXSTYLE) & WS_EX_TOOLWINDOW) return TRUE;
        DWORD cloaked=0;
        if (SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_CLOAKED,&cloaked,sizeof(cloaked))) && cloaked) return TRUE;
        int length=GetWindowTextLengthW(window);
        if (length<=0) return TRUE;
        std::wstring title(static_cast<size_t>(length)+1,L'\0');
        int copied=GetWindowTextW(window,title.data(),length+1);
        if (copied<=0) return TRUE;
        title.resize(static_cast<size_t>(copied));
        title+=L" (PID "+std::to_wstring(process)+L")";
        self.sources.push_back({nullptr,window,process,title});
        return TRUE;
    }
    void RefreshSources() {
        Source previous{};
        auto selected=SendMessageW(sourceBox,CB_GETCURSEL,0,0);
        if (selected>=0 && static_cast<size_t>(selected)<sources.size()) previous=sources[static_cast<size_t>(selected)];
        sources.clear();
        SendMessageW(sourceBox,CB_RESETCONTENT,0,0);
        if (SendMessageW(sourceKindBox,CB_GETCURSEL,0,0)==0)
            EnumDisplayMonitors(nullptr,nullptr,AddMonitor,reinterpret_cast<LPARAM>(this));
        else EnumWindows(AddWindow,reinterpret_cast<LPARAM>(this));
        size_t selection=0;
        for (size_t i=0;i<sources.size();++i) {
            SendMessageW(sourceBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(sources[i].label.c_str()));
            if ((previous.monitor && previous.monitor==sources[i].monitor) ||
                (previous.window && previous.window==sources[i].window && previous.process==sources[i].process)) selection=i;
        }
        if (!sources.empty()) SendMessageW(sourceBox,CB_SETCURSEL,selection,0);
        EnableWindow(GetDlgItem(controls,kPick),!sources.empty());
        if (sources.empty() && !running) Status(L"Nenhuma origem disponível. Abra o aplicativo e clique em Atualizar.");
        PublishWebState();
    }
    UINT generation = 0, fps = 30;
    float exposure = 0, white = 203;
    com_ptr<ID3D11Device> device;
    com_ptr<ID3D11DeviceContext> context;
    com_ptr<IDXGIFactory2> factory;
    com_ptr<IDXGISwapChain1> swap, previewSwap;
    com_ptr<ID3D11RenderTargetView> target, previewTarget;
    bridge::VideoFrame programFrame;
    com_ptr<ID3D11VertexShader> vs;
    com_ptr<ID3D11PixelShader> ps, displayShader;
    com_ptr<ID3D11SamplerState> sampler;
    com_ptr<ID3D11Buffer> constants;
    com_ptr<ID3D11Texture2D> copied;
    com_ptr<ID3D11ShaderResourceView> image;
    UINT imageWidth = 0, imageHeight = 0, outputWidth = 0, outputHeight = 0;
    UINT previewWidth = 0, previewHeight = 0;
    IDirect3DDevice winDevice{nullptr};
    GraphicsCaptureItem item{nullptr};
    Direct3D11CaptureFramePool pool{nullptr};
    GraphicsCaptureSession session{nullptr};
    winrt::Windows::Graphics::SizeInt32 poolSize{};
    event_token closedToken{};

    void Status(std::wstring const& message) {
        videoStatusText=message;
        SetWindowTextW(status,message.c_str());
        if (webActive) SendText(L"status",message);
    }
    void Fail(hresult_error const& error) {
        Stop();
        Status(L"Falha. Se o dispositivo foi removido, reinicie o app.");
        MessageBoxW(controls, error.message().c_str(), L"HDR Bridge", MB_ICONERROR);
    }
    void InitGraphics() {
        D3D_FEATURE_LEVEL level;
        check_hresult(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0, D3D11_SDK_VERSION,
            device.put(), &level, context.put()));
        auto dxgi = device.as<IDXGIDevice>();
        com_ptr<IDXGIAdapter> adapter;
        check_hresult(dxgi->GetAdapter(adapter.put()));
        check_hresult(adapter->GetParent(__uuidof(IDXGIFactory2), factory.put_void()));
        com_ptr<::IInspectable> inspectable;
        check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgi.get(), inspectable.put()));
        winDevice = inspectable.as<IDirect3DDevice>();
        auto vertex = Compile("VS", "vs_5_0"), pixel = Compile("PS", "ps_5_0");
        check_hresult(device->CreateVertexShader(vertex->GetBufferPointer(), vertex->GetBufferSize(), nullptr, vs.put()));
        check_hresult(device->CreatePixelShader(pixel->GetBufferPointer(), pixel->GetBufferSize(), nullptr, ps.put()));
        auto display=Compile("DisplayPS","ps_5_0");
        check_hresult(device->CreatePixelShader(display->GetBufferPointer(),display->GetBufferSize(),nullptr,displayShader.put()));
        D3D11_SAMPLER_DESC sample{};
        sample.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sample.AddressU = sample.AddressV = sample.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sample.MaxLOD = D3D11_FLOAT32_MAX;
        check_hresult(device->CreateSamplerState(&sample, sampler.put()));
        D3D11_BUFFER_DESC buffer{};
        buffer.ByteWidth = 16; buffer.Usage = D3D11_USAGE_DEFAULT;
        buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check_hresult(device->CreateBuffer(&buffer, nullptr, constants.put()));
    }
    void ReleaseSwap() {
        context->OMSetRenderTargets(0, nullptr, nullptr);
        target = nullptr; swap = nullptr;
        previewTarget = nullptr; previewSwap = nullptr;
        programFrame.Reset(); compatibility.Clear();
        context->ClearState(); context->Flush();
        outputWidth = outputHeight = previewWidth = previewHeight = 0;
    }
    void EnsureSurface(HWND destination, UINT width, UINT height,
        com_ptr<IDXGISwapChain1>& swap, com_ptr<ID3D11RenderTargetView>& target,
        UINT& outputWidth, UINT& outputHeight) {
        if (swap && width == outputWidth && height == outputHeight) return;
        context->OMSetRenderTargets(0, nullptr, nullptr);
        target = nullptr;
        if (!swap) {
            DXGI_SWAP_CHAIN_DESC1 desc{};
            desc.Width = width; desc.Height = height;
            desc.Format = hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            desc.Scaling = DXGI_SCALING_STRETCH; desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
            check_hresult(factory->CreateSwapChainForHwnd(device.get(), destination, &desc, nullptr, nullptr, swap.put()));
            check_hresult(factory->MakeWindowAssociation(destination, DXGI_MWA_NO_ALT_ENTER));
            auto swap3 = swap.as<IDXGISwapChain3>();
            auto space = hdr ? DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 : DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
            UINT support = 0;
            check_hresult(swap3->CheckColorSpaceSupport(space, &support));
            if (!(support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
                throw hresult_error(E_NOTIMPL, L"Espaço de cor indisponível para esta saída.");
            check_hresult(swap3->SetColorSpace1(space));
        } else {
            check_hresult(swap->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0));
        }
        com_ptr<ID3D11Texture2D> back;
        check_hresult(swap->GetBuffer(0, __uuidof(ID3D11Texture2D), back.put_void()));
        check_hresult(device->CreateRenderTargetView(back.get(), nullptr, target.put()));
        outputWidth = width; outputHeight = height;
    }
    void Stop() {
        audio.Stop(); audioConfigError.clear(); activeProcess=0;
        activeSourceWindow=nullptr;
        RefreshAudioStatus();
        compatibility.Clear();
        if (frameTimer) CancelWaitableTimer(frameTimer);
        running = false; ++generation;
        if (item) { item.Closed(closedToken); item = nullptr; }
        if (session) { session.Close(); session = nullptr; }
        if (pool) { pool.Close(); pool = nullptr; }
        ID3D11ShaderResourceView* none = nullptr;
        if (context) context->PSSetShaderResources(0, 1, &none);
        image = nullptr; copied = nullptr;
        imageWidth = imageHeight = 0;
        if (context) {
            programFrame.Clear(context.get());
            float black[4]={0,0,0,1};
            if (target) { context->ClearRenderTargetView(target.get(),black); swap->Present(0,0); }
            if (previewTarget) { context->ClearRenderTargetView(previewTarget.get(),black); previewSwap->Present(0,0); }
        }
        HideOutputToolbar();
        ShowWindow(output, SW_HIDE);
        outputLowered=false;
        EnableWindow(GetDlgItem(controls,kShow),FALSE);
        EnableWindow(GetDlgItem(controls,kHideOutput),FALSE);
        EnableWindow(GetDlgItem(controls,kLowerOutput),FALSE);
        Status(L"Parado. Escolha a janela ou monitor de origem.");
        PublishWebState();
    }
    void StartSelected() {
        auto selected=SendMessageW(sourceBox,CB_GETCURSEL,0,0);
        if (selected<0 || static_cast<size_t>(selected)>=sources.size()) {
            MessageBoxW(controls,L"Selecione um monitor ou aplicativo.",L"HDR Bridge",MB_ICONINFORMATION);
            return;
        }
        auto source=sources[static_cast<size_t>(selected)];
        GraphicsCaptureItem chosen{nullptr};
        auto interop=get_activation_factory<GraphicsCaptureItem,IGraphicsCaptureItemInterop>();
        if (source.monitor) {
            MONITORINFO info{}; info.cbSize=sizeof(info);
            if (!GetMonitorInfoW(source.monitor,&info)) {
                RefreshSources();
                MessageBoxW(controls,L"Monitor desconectado. Escolha novamente.",L"HDR Bridge",MB_ICONINFORMATION);
                return;
            }
            check_hresult(interop->CreateForMonitor(source.monitor,guid_of<GraphicsCaptureItem>(),put_abi(chosen)));
        } else {
            DWORD process=0; GetWindowThreadProcessId(source.window,&process);
            if (!IsWindow(source.window) || process!=source.process) {
                RefreshSources();
                MessageBoxW(controls,L"A janela foi fechada. Escolha novamente.",L"HDR Bridge",MB_ICONINFORMATION);
                return;
            }
            check_hresult(interop->CreateForWindow(source.window,guid_of<GraphicsCaptureItem>(),put_abi(chosen)));
        }
        Stop();
        item=chosen;
        auto serial=generation;
        closedToken=item.Closed([this,serial](auto const&,auto const&) { PostMessageW(controls,kClosed,serial,0); });
        poolSize=item.Size();
        pool=Direct3D11CaptureFramePool::CreateFreeThreaded(winDevice,DirectXPixelFormat::R16G16B16A16Float,2,poolSize);
        session=pool.CreateCaptureSession(item);
        session.IsCursorCaptureEnabled(!gdiOutput);
        // Starting capture opens only the embedded preview. The large share
        // window is opt-in and never silently reopened after a source change.
        session.StartCapture();
        running=true;
        activeProcess=source.process;
        activeSourceWindow=source.window;
        RestartAudio();
        EnableWindow(GetDlgItem(controls,kShow),TRUE);
        ArmTimer(true);
        UpdateStatus();
    }
    void UpdateStatus() {
        if (!running) return;
        std::wstring text = L"Origem: "; text += item.DisplayName().c_str();
        text += hdr ? L"\r\nHDR scRGB local. A transmissão depende do serviço."
                    : gdiOutput ? L"\r\nSDR compatível (GDI; maior uso de CPU)." : L"\r\nSDR com tone mapping.";
        if (IsIconic(output)) text+=L"\r\nSaída minimizada. Restaure para compartilhar.";
        else if (outputLowered) text+=L"\r\nSaída atrás das janelas. Vídeo ativo.";
        else if (IsWindowVisible(output)) text+=L"\r\nJanela de saída aberta.";
        else text+=L"\r\nPrévia interna; janela de saída fechada.";
        auto extent=bridge::OutputExtent(resolution);
        text+=L"\r\nSaída: "+std::to_wstring(extent.width)+L" × "+std::to_wstring(extent.height)+
            L" · "+std::to_wstring(fps)+L" fps (alvo)";
        Status(text);
        PublishWebState();
        SetWindowTextW(output,hdr ? L"HDR Bridge - Saida HDR local" : gdiOutput ? L"HDR Bridge - Saida SDR - Compatibilidade" : L"HDR Bridge - Saida SDR");
    }
    void Tick() {
        if (!running) return;
        bool showPreview=IsWindowVisible(preview) && !IsIconic(controls);
        bool showOutput=IsWindowVisible(output) && !IsIconic(output);
        if (!showPreview && !showOutput) return;
        ID3D11ShaderResourceView* none = nullptr;
        auto frame = pool.TryGetNextFrame();
        if (frame) {
            // Keep only the freshest of the two available frames.
            if (auto newer = pool.TryGetNextFrame()) { frame.Close(); frame = newer; }
            auto size = frame.ContentSize();
            if (size.Width <= 0 || size.Height <= 0) { frame.Close(); return; }
            if (size.Width != poolSize.Width || size.Height != poolSize.Height) {
                frame.Close(); poolSize = size;
                pool.Recreate(winDevice, DirectXPixelFormat::R16G16B16A16Float, 2, size);
                return;
            }
            auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            com_ptr<ID3D11Texture2D> texture;
            check_hresult(access->GetInterface(__uuidof(ID3D11Texture2D), texture.put_void()));
            D3D11_TEXTURE2D_DESC sourceDesc{}; texture->GetDesc(&sourceDesc);
            UINT width = std::min(static_cast<UINT>(size.Width), sourceDesc.Width);
            UINT height = std::min(static_cast<UINT>(size.Height), sourceDesc.Height);
            context->PSSetShaderResources(0, 1, &none);
            if (!copied || width != imageWidth || height != imageHeight) {
                image = nullptr; copied = nullptr;
                D3D11_TEXTURE2D_DESC desc{};
                desc.Width = width; desc.Height = height;
                desc.MipLevels = desc.ArraySize = 1; desc.SampleDesc.Count = 1;
                desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                check_hresult(device->CreateTexture2D(&desc, nullptr, copied.put()));
                check_hresult(device->CreateShaderResourceView(copied.get(), nullptr, image.put()));
                imageWidth = width; imageHeight = height;
            }
            D3D11_BOX box{0, 0, 0, width, height, 1};
            context->CopySubresourceRegion(copied.get(), 0, 0, 0, 0, texture.get(), 0, &box);
            frame.Close();
        }
        if (!image) return;
        auto extent=bridge::OutputExtent(resolution);
        programFrame.Ensure(device.get(),extent,hdr);
        float values[4]={hdr ? 1.0f : 0.0f,exposure,white,0.65f};
        context->UpdateSubresource(constants.get(),0,nullptr,values,0,0);
        programFrame.Process(context.get(),image.get(),{imageWidth,imageHeight},
            vs.get(),ps.get(),sampler.get(),constants.get());
        if (showPreview) {
            RECT client{}; GetClientRect(preview,&client);
            if (client.right>0 && client.bottom>0) {
                auto width=static_cast<UINT>(client.right),height=static_cast<UINT>(client.bottom);
                EnsureSurface(preview,width,height,previewSwap,previewTarget,previewWidth,previewHeight);
                programFrame.Blit(context.get(),previewTarget.get(),{width,height},vs.get(),displayShader.get(),sampler.get());
                check_hresult(previewSwap->Present(0,0));
            }
        }
        if (showOutput) {
            if (gdiOutput) {
                if (compatibility.Update(device.get(),context.get(),programFrame.Texture()))
                    InvalidateRect(output,nullptr,FALSE);
            } else {
                EnsureSurface(output,extent.width,extent.height,swap,target,outputWidth,outputHeight);
                programFrame.Blit(context.get(),target.get(),extent,vs.get(),displayShader.get(),sampler.get());
                check_hresult(swap->Present(0,0));
            }
        }
    }
    HWND Child(wchar_t const* klass, wchar_t const* text, DWORD style, int x, int y, int w, int h, int id = 0, HWND parent = nullptr) {
        if (!parent || parent!=outputToolbar) {
            if (wcscmp(klass,WC_BUTTONW)==0) style|=BS_OWNERDRAW;
            else if (wcscmp(klass,WC_COMBOBOXW)==0) style|=CBS_OWNERDRAWFIXED|CBS_HASSTRINGS;
        }
        HWND child = CreateWindowExW(0, klass, text, WS_CHILD|WS_VISIBLE|style, x,y,w,h,
            parent ? parent : controls, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        if (!child) throw_last_error();
        SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return child;
    }
    void InitUI() {
        closeChoice=static_cast<CloseChoice>(ReadSetting(L"CloseChoice",static_cast<DWORD>(CloseChoice::Exit)));
        theme.choice=static_cast<bridge::Theme>(ReadSetting(L"Theme",static_cast<DWORD>(bridge::Theme::Light)));
        Child(L"STATIC",L"HDR Bridge  /  configurar captura",0,20,18,275,24);
        Child(L"STATIC",L"Tema",0,313,19,44,22);
        themeBox=Child(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,360,16,130,110,kTheme);
        SendMessageW(themeBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Escuro"));
        SendMessageW(themeBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Claro"));
        SendMessageW(themeBox,CB_SETCURSEL,static_cast<WPARAM>(theme.choice),0);
        theme.Refresh(controls);
        Child(L"BUTTON",L"Perguntar ao fechar",WS_TABSTOP,510,16,180,27,kResetClose);
        Child(L"STATIC",L"Transmitir",0,20,58,130,22);
        sourceKindBox=Child(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,160,52,270,120,kSourceKind);
        SendMessageW(sourceKindBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Monitor"));
        SendMessageW(sourceKindBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"Aplicativo (janela)"));
        SendMessageW(sourceKindBox,CB_SETCURSEL,1,0);
        Child(L"BUTTON",L"Atualizar lista",WS_TABSTOP,450,52,240,28,kRefresh);
        Child(L"STATIC",L"Origem",0,20,104,120,22);
        sourceBox=Child(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP|WS_VSCROLL,160,98,530,300,kSource);
        SendMessageW(sourceBox,CB_SETDROPPEDWIDTH,700,0);
        Child(L"STATIC",L"Resolução de saída",0,20,150,135,22);
        resolutionBox=Child(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,160,144,270,120,kResolution);
        SendMessageW(resolutionBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"720p — 1280 × 720"));
        SendMessageW(resolutionBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"1080p — 1920 × 1080"));
        SendMessageW(resolutionBox,CB_SETCURSEL,0,0);
        rateBox=Child(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,450,144,240,120,kRate);
        SendMessageW(rateBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"30 fps"));
        SendMessageW(rateBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"60 fps"));
        SendMessageW(rateBox,CB_SETCURSEL,0,0);
        Child(L"STATIC",L"Cor da saída",0,20,196,130,22);
        modeBox=Child(WC_COMBOBOXW,L"",CBS_DROPDOWNLIST|WS_TABSTOP,160,190,530,130,kMode);
        SendMessageW(modeBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"SDR — tone mapping para compartilhar"));
        SendMessageW(modeBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"HDR — preservar scRGB (saída local)"));
        SendMessageW(modeBox,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"SDR — compatibilidade GDI (maior uso de CPU)"));
        SendMessageW(modeBox,CB_SETCURSEL,0,0);
        exposureLabel=Child(L"STATIC",L"Exposição: 0.0 EV",0,20,244,190,22);
        exposureSlider=Child(TRACKBAR_CLASSW,L"",TBS_AUTOTICKS|WS_TABSTOP,225,236,465,34,kExposure);
        SendMessageW(exposureSlider,TBM_SETRANGE,TRUE,MAKELPARAM(-20,20));
        SendMessageW(exposureSlider,TBM_SETPOS,TRUE,0);
        whiteLabel=Child(L"STATIC",L"Branco ref.: 203 nits",0,20,290,195,22);
        whiteSlider=Child(TRACKBAR_CLASSW,L"",TBS_AUTOTICKS|WS_TABSTOP,225,282,465,34,kWhite);
        SendMessageW(whiteSlider,TBM_SETRANGE,TRUE,MAKELPARAM(80,400));
        SendMessageW(whiteSlider,TBM_SETPOS,TRUE,203);
        Child(L"BUTTON",L"Iniciar / trocar origem",WS_TABSTOP,20,332,230,32,kPick);
        Child(L"BUTTON",L"Parar",WS_TABSTOP,270,332,120,32,kStop);
        Child(L"BUTTON",L"Áudio...",WS_TABSTOP,450,332,240,32,kAudio);
        Child(L"STATIC",L"Prévia · resolução de processamento independente",0,20,374,330,22);
        preview=CreateWindowExW(0,L"HdrBridgePreview",L"",WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,
            20,400,320,180,controls,nullptr,GetModuleHandleW(nullptr),nullptr);
        if (!preview) throw_last_error();
        status=Child(L"STATIC",L"Parado. Selecione a origem e clique em Iniciar.",0,360,378,330,100);
        Child(L"STATIC",L"Passe o mouse no canto superior direito da saída.\r\nUse Recolher para pôr o vídeo atrás das janelas.\r\nTela branca em outro app? Teste SDR GDI.",0,360,488,330,70);
        audioSummary=Child(L"STATIC",L"Áudio desligado",0,360,564,330,24);
        Child(L"BUTTON",L"Abrir saída para Discord",WS_TABSTOP,20,600,240,32,kShow);
        Child(L"BUTTON",L"Recolher saída",WS_TABSTOP,270,600,130,32,kLowerOutput);
        Child(L"BUTTON",L"Fechar saída",WS_TABSTOP,410,600,130,32,kHideOutput);
        Child(L"BUTTON",L"Sair",WS_TABSTOP,570,600,120,32,kExit);
        EnableWindow(GetDlgItem(controls,kShow),FALSE);
        EnableWindow(GetDlgItem(controls,kHideOutput),FALSE);
        EnableWindow(GetDlgItem(controls,kLowerOutput),FALSE);
        RefreshSources();
        // Keep the controller and local toolbar outside the shared image.
        SetWindowDisplayAffinity(controls, WDA_EXCLUDEFROMCAPTURE);
        outputToolbar=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"HdrBridgeWindow",L"",
            WS_POPUP|WS_CLIPCHILDREN,0,0,310,38,output,nullptr,GetModuleHandleW(nullptr),nullptr);
        if (outputToolbar && SetWindowDisplayAffinity(outputToolbar,WDA_EXCLUDEFROMCAPTURE)) {
            outputToolbarTitle=Child(L"STATIC",L"HDR Bridge",0,10,10,120,22,0,outputToolbar);
            outputToolbarLower=Child(L"BUTTON",L"Recolher",0,133,5,100,28,kLowerToolbar,outputToolbar);
            outputToolbarClose=Child(L"BUTTON",L"Fechar",0,238,5,65,28,kHideOutput,outputToolbar);
        } else if (outputToolbar) {
            DestroyWindow(outputToolbar);
            outputToolbar=nullptr;
        }
        NOTIFYICONDATAW icon{sizeof(icon)};
        icon.hWnd = controls; icon.uID = 1; icon.uFlags = NIF_ICON|NIF_MESSAGE|NIF_TIP;
        icon.uCallbackMessage = kTray; icon.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(icon.szTip,L"HDR Bridge — clique para abrir controles");
        Shell_NotifyIconW(NIM_ADD, &icon);
    }
    void SendText(std::wstring_view type,std::wstring_view value) {
        web.Send(L"{\"type\":"+bridge::JsonString(type)+L",\"text\":"+bridge::JsonString(value)+L"}");
    }
    void PublishWebState() {
        if (!webActive) return;
        std::wstring message=L"{\"type\":\"state\",\"sources\":[";
        for (size_t i=0;i<sources.size();++i) {
            if (i) message+=L',';
            message+=bridge::JsonString(sources[i].label);
        }
        message+=L']';
        auto selection=[&](std::wstring_view name,HWND control,UINT read=CB_GETCURSEL) {
            message+=L",\""+std::wstring(name)+L"\":"+std::to_wstring(SendMessageW(control,read,0,0));
        };
        selection(L"source-kind",sourceKindBox);
        selection(L"source",sourceBox);
        selection(L"resolution",resolutionBox);
        selection(L"rate",rateBox);
        selection(L"mode",modeBox);
        selection(L"exposure",exposureSlider,TBM_GETPOS);
        selection(L"white",whiteSlider,TBM_GETPOS);
        message+=L",\"theme\":"+std::to_wstring(static_cast<DWORD>(theme.choice));
        message+=running ? L",\"running\":true" : L",\"running\":false";
        message+=IsWindowVisible(output) ? L",\"output\":true}" : L",\"output\":false}";
        web.Send(message);
    }
    void PublishAudioSettings() {
        std::wstring message=L"{\"type\":\"audio-settings\",\"destinations\":[\"Não selecionado\"";
        int selected=0;
        for (size_t i=0;i<audioDestinations.size();++i) {
            message+=L','+bridge::JsonString(audioDestinations[i].name);
            if (audioDestinations[i].id==audioDestination) selected=static_cast<int>(i)+1;
        }
        message+=L"],\"destination\":"+std::to_wstring(selected)+L",\"mode\":"+std::to_wstring(static_cast<int>(audioSource))+
            L",\"gain\":"+std::to_wstring(SendMessageW(audioGainSlider,TBM_GETPOS,0,0))+L'}';
        web.Send(message);
    }
    void StartWebUi() {
        if (webHost) return;
        webHost=CreateWindowExW(0,L"STATIC",L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
            0,0,1,1,controls,nullptr,GetModuleHandleW(nullptr),nullptr);
        if (!webHost) return;
        SetTimer(controls,kWebStartupTimer,15000,nullptr);
        UpdateUiMenu();
        ResizeWebUi();
        web.Start(webHost,controls,kWebCommand);
    }
    void ResizeWebUi() {
        if (!webHost) return;
        RECT area{};
        GetClientRect(controls,&area);
        if (area.right>0 && area.bottom>0) {
            SetWindowPos(webHost,nullptr,0,0,area.right,area.bottom,SWP_NOZORDER|SWP_NOACTIVATE);
            web.Resize();
        }
    }
    void PresentWebUi() {
        if (!webHost || webPresenting || webActive) return;
        ResizeWebUi();
        UpdateThemeBackground();
        webPresenting=true;
        if (!SetWindowPos(webHost,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW) ||
            !web.Present()) UseNativeUi();
    }
    void ActivateWebUi() {
        if (!webPresenting) return;
        webPresenting=false;
        KillTimer(controls,kWebStartupTimer);
        webActive=true;
        auto style=GetWindowLongPtrW(controls,GWL_STYLE);
        SetWindowLongPtrW(controls,GWL_STYLE,style|WS_THICKFRAME|WS_MAXIMIZEBOX);
        auto dpi=GetDpiForWindow(controls);
        MONITORINFO monitor{}; monitor.cbSize=sizeof(monitor);
        auto width=MulDiv(930,static_cast<int>(dpi),96),height=MulDiv(615,static_cast<int>(dpi),96);
        if (GetMonitorInfoW(MonitorFromWindow(controls,MONITOR_DEFAULTTONEAREST),&monitor)) {
            width=std::min(width,static_cast<int>(monitor.rcWork.right-monitor.rcWork.left));
            height=std::min(height,static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top));
        }
        SetWindowPos(controls,nullptr,0,0,width,height,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        ResizeWebUi();
        SetWindowPos(webHost,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
        for (HWND child=GetWindow(controls,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
            if (child!=webHost) ShowWindow(child,SW_HIDE);
        }
        PublishWebState();
        SendText(L"status",videoStatusText);
        SendText(L"audio-status",audioStatusText.empty() ? L"Áudio desligado" : audioStatusText);
    }
    void UpdateThemeBackground() const {
        web.SetBackground(theme.choice==bridge::Theme::Dark ?
            COREWEBVIEW2_COLOR{255,30,32,37} : COREWEBVIEW2_COLOR{255,240,241,244});
    }
    void ChangeTheme(bridge::Theme choice) {
        if (choice==theme.choice) return;
        theme.choice=choice;
        SendMessageW(themeBox,CB_SETCURSEL,static_cast<WPARAM>(choice),0);
        theme.Refresh(controls);
        theme.Refresh(audioWindow);
        UpdateThemeBackground();
        PublishWebState();
        if (!SaveSetting(L"Theme",static_cast<DWORD>(choice)))
            MessageBoxW(controls,L"Não foi possível salvar o tema para a próxima abertura.",L"HDR Bridge",MB_ICONWARNING);
    }
    void UpdateUiMenu() const {
        auto menu=GetSystemMenu(controls,FALSE);
        RemoveMenu(menu,kSwitchUi,MF_BYCOMMAND);
        InsertMenuW(menu,SC_CLOSE,MF_BYCOMMAND|MF_STRING,kSwitchUi,
            webHost ? L"Usar interface nativa" : L"Usar interface moderna");
    }
    void ToggleUi() {
        if (webHost) {
            SaveSetting(L"NativeUi",1);
            UseNativeUi();
        } else {
            SaveSetting(L"NativeUi",0);
            StartWebUi();
        }
    }
    void UseNativeUi() {
        KillTimer(controls,kWebStartupTimer);
        webPresenting=false;
        web.Close();
        if (webHost) DestroyWindow(webHost);
        webHost=nullptr;
        UpdateUiMenu();
        if (!webActive) return;
        webActive=false;
        confirmingClose=false;
        SetWindowPos(preview,nullptr,20,400,320,180,SWP_NOZORDER|SWP_NOACTIVATE);
        for (HWND child=GetWindow(controls,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT)) {
            ShowWindow(child,SW_SHOWNA);
        }
        if (IsZoomed(controls)) ShowWindow(controls,SW_RESTORE);
        auto style=GetWindowLongPtrW(controls,GWL_STYLE);
        SetWindowLongPtrW(controls,GWL_STYLE,style & ~(WS_THICKFRAME|WS_MAXIMIZEBOX));
        SetWindowPos(controls,nullptr,0,0,740,690,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
    }
    void ApplyPreviewLayout(std::span<int const,4> values) {
        auto x=values[0],y=values[1],width=values[2],height=values[3];
        RECT area{}; GetClientRect(controls,&area);
        if (x<0 || y<0 || width<=0 || height<=0 || x>area.right || y>area.bottom ||
            width>area.right-x || height>area.bottom-y) {
            ShowWindow(preview,SW_HIDE);
            return;
        }
        SetWindowPos(preview,HWND_TOP,x,y,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
    void ResetCloseChoice() {
        if (!SaveSetting(L"CloseChoice",static_cast<DWORD>(CloseChoice::Ask))) {
            MessageBoxW(controls,L"Não foi possível salvar a preferência.",L"HDR Bridge",MB_ICONWARNING);
            return;
        }
        closeChoice=CloseChoice::Ask;
        MessageBoxW(controls,L"O HDR Bridge voltará a perguntar ao fechar.",L"HDR Bridge",MB_OK|MB_ICONINFORMATION);
    }
    void FinishClose(CloseChoice choice,bool remember) {
        confirmingClose=false;
        if (choice==CloseChoice::Ask) return;
        if (remember) {
            if (SaveSetting(L"CloseChoice",static_cast<DWORD>(choice))) closeChoice=choice;
            else MessageBoxW(controls,L"Não foi possível salvar a preferência.",L"HDR Bridge",MB_ICONWARNING);
        }
        if (choice==CloseChoice::Minimize) ShowWindow(controls,SW_HIDE);
        else Command(kExit,0);
    }
    void ConfirmClose() {
        if (confirmingClose) return;
        if (closeChoice!=CloseChoice::Ask) { FinishClose(closeChoice,false); return; }
        confirmingClose=true;
        if (webActive) {
            ShowWindow(preview,SW_HIDE);
            web.Send(L"{\"type\":\"confirm-close\"}");
            return;
        }
        TASKDIALOG_BUTTON buttons[]={{kShow,L"Minimizar"},{kExit,L"Sair"}};
        TASKDIALOGCONFIG dialog{};
        dialog.cbSize=sizeof(dialog); dialog.hwndParent=controls;
        dialog.dwFlags=TDF_POSITION_RELATIVE_TO_WINDOW|TDF_ALLOW_DIALOG_CANCELLATION;
        dialog.dwCommonButtons=TDCBF_CANCEL_BUTTON;
        dialog.pszWindowTitle=L"HDR Bridge";
        dialog.pszMainInstruction=L"Minimizar ou sair?";
        dialog.pszContent=L"Minimizar mantém a captura ativa na bandeja do Windows. Sair encerra a imagem e o áudio.";
        dialog.pszVerificationText=L"Salvar minha escolha";
        dialog.cButtons=2; dialog.pButtons=buttons; dialog.nDefaultButton=IDCANCEL;
        int button=IDCANCEL; BOOL remember=FALSE;
        // The native dialog runs a modal message loop. Keep sharing frames
        // while it waits for a choice, even without the WebView interface.
        if (running) SetTimer(controls,kCloseDialogTimer,std::max(1u,1000u/fps),nullptr);
        TaskDialogIndirect(&dialog,&button,nullptr,&remember);
        KillTimer(controls,kCloseDialogTimer);
        FinishClose(button==kShow ? CloseChoice::Minimize : button==kExit ? CloseChoice::Exit : CloseChoice::Ask,remember!=FALSE);
    }
    void WebCommand(std::wstring_view message) {
        if (message==L"ready") { PresentWebUi(); return; }
        if (message==L"presented") { ActivateWebUi(); return; }
        if (!webActive) return;
        auto text=to_string(message);
        std::string_view command=text;
        auto delimiter=command.find(':');
        auto action=command.substr(0,delimiter);
        auto arguments=delimiter==std::string_view::npos ? std::string_view{} : command.substr(delimiter+1);
        if (action=="layout") {
            int values[4]{};
            if (ParseValues(arguments,values)) ApplyPreviewLayout(values);
            return;
        }
        if (action=="theme") {
            if (arguments=="0") ChangeTheme(bridge::Theme::Dark);
            else if (arguments=="1") ChangeTheme(bridge::Theme::Light);
            return;
        }
        if (action=="close-cancel") { confirmingClose=false; return; }
        if (action=="close-minimize" || action=="close-exit") {
            int value=0;
            if (confirmingClose && ParseValues(arguments,{&value,1}) && (value==0 || value==1))
                FinishClose(action=="close-minimize" ? CloseChoice::Minimize : CloseChoice::Exit,value==1);
            return;
        }
        if (action=="audio-open" || action=="audio-refresh") {
            OpenAudio(false);
            if (action=="audio-refresh") RefreshAudioDestinations();
            PublishAudioSettings();
            return;
        }
        if (action=="audio-apply") {
            int values[3]{};
            if (audioWindow && ParseValues(arguments,values) && values[0]>=0 && values[0]<=2 &&
                values[1]>=0 && static_cast<size_t>(values[1])<=audioDestinations.size() && values[2]>=0 && values[2]<=200) {
                SendMessageW(audioModeBox,CB_SETCURSEL,values[0],0);
                SendMessageW(audioDestinationBox,CB_SETCURSEL,values[1],0);
                SendMessageW(audioGainSlider,TBM_SETPOS,TRUE,values[2]);
                ApplyAudio();
            }
            return;
        }
        if (arguments.empty()) {
            if (action=="start") Command(kPick,0);
            else if (action=="stop") Command(kStop,0);
            else if (action=="show") Command(kShow,0);
            else if (action=="lower") Command(kLowerOutput,0);
            else if (action=="hide") Command(kHideOutput,0);
            else if (action=="refresh") Command(kRefresh,0);
            else if (action=="reset-close") Command(kResetClose,0);
            else if (action=="exit") Command(kExit,0);
            return;
        }
        int value=0;
        if (!ParseValues(arguments,{&value,1})) return;
        if (action=="exposure" && value>=-20 && value<=20) {
            SendMessageW(exposureSlider,TBM_SETPOS,TRUE,value); Sliders(); return;
        }
        if (action=="white" && value>=80 && value<=400) {
            SendMessageW(whiteSlider,TBM_SETPOS,TRUE,value); Sliders(); return;
        }
        auto select=[&](HWND control,int id,int maxValue) {
            if (value>=0 && value<=maxValue) { SendMessageW(control,CB_SETCURSEL,value,0); Command(id,CBN_SELCHANGE); }
        };
        if (action=="source-kind") select(sourceKindBox,kSourceKind,1);
        else if (action=="source") select(sourceBox,kSource,static_cast<int>(sources.size())-1);
        else if (action=="resolution") select(resolutionBox,kResolution,1);
        else if (action=="rate") select(rateBox,kRate,1);
        else if (action=="mode") select(modeBox,kMode,2);
    }
    void Command(int id, int event) {
        if (id == kResetClose) ResetCloseChoice();
        if (id == kTheme && event == CBN_SELCHANGE) {
            auto choice=SendMessageW(themeBox,CB_GETCURSEL,0,0);
            if (choice!=CB_ERR) ChangeTheme(static_cast<bridge::Theme>(choice));
        }
        if (id == kAudio) OpenAudio();
        if (id == kAudioRefresh) RefreshAudioDestinations();
        if (id == kAudioApply) ApplyAudio();
        if (id == kPick) StartSelected();
        if (id == kRefresh || (id == kSourceKind && event == CBN_SELCHANGE)) RefreshSources();
        if (id == kStop) Stop();
        if (id == kShow && running) {
            if (IsIconic(output)) ShowWindow(output,SW_RESTORE);
            FitOutputWindow();
            // Bring back the same shareable HWND so an existing capture can
            // keep following it throughout minimize and restore.
            if (!SetWindowPos(output,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW))
                throw_last_error();
            outputLowered=false;
            EnableWindow(GetDlgItem(controls,kHideOutput),TRUE);
            EnableWindow(GetDlgItem(controls,kLowerOutput),TRUE);
            Tick(); UpdateStatus();
        }
        if (id == kHideOutput) CloseOutput();
        if ((id == kLowerOutput || id == kLowerToolbar) && running) LowerOutput();
        if (id == kExit) { Stop(); web.Close(); DestroyWindow(controls); return; }
        if (id == kMode && event == CBN_SELCHANGE) {
            auto selected=SendMessageW(modeBox,CB_GETCURSEL,0,0);
            hdr=selected==1; gdiOutput=selected==2;
            if (session) session.IsCursorCaptureEnabled(!gdiOutput);
            EnableWindow(exposureSlider,!hdr); EnableWindow(whiteSlider,!hdr);
            ReleaseSwap(); UpdateStatus();
        }
        if (id == kRate && event == CBN_SELCHANGE) {
            fps = SendMessageW(rateBox,CB_GETCURSEL,0,0) == 1 ? 60 : 30;
            if (running) ArmTimer(true);
            UpdateStatus();
        }
        if (id == kResolution && event == CBN_SELCHANGE) {
            resolution=SendMessageW(resolutionBox,CB_GETCURSEL,0,0)==1 ? bridge::Resolution::P1080 : bridge::Resolution::P720;
            ReleaseSwap(); FitOutputWindow(); UpdateStatus();
        }
    }
    void LowerOutput() {
        // A true Win32 minimize can withdraw the window's content from other
        // programs. Lower this window while keeping its size and visibility.
        HideOutputToolbar();
        outputLowered=true;
        if (!SetWindowPos(output,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE))
            throw_last_error();
        if (GetForegroundWindow()==output) {
            DWORD process=0;
            if (activeSourceWindow) GetWindowThreadProcessId(activeSourceWindow,&process);
            if (process && process==activeProcess && IsWindowVisible(activeSourceWindow) && !IsIconic(activeSourceWindow))
                SetForegroundWindow(activeSourceWindow);
            else if (IsWindowVisible(controls)) SetForegroundWindow(controls);
            if (GetActiveWindow()==output) SetActiveWindow(nullptr);
        }
        UpdateStatus();
    }
    void CloseOutput() {
        HideOutputToolbar();
        compatibility.Clear();
        if (target) {
            float black[4]={0,0,0,1}; context->ClearRenderTargetView(target.get(),black); swap->Present(0,0);
        }
        ShowWindow(output,SW_HIDE);
        outputLowered=false;
        EnableWindow(GetDlgItem(controls,kHideOutput),FALSE);
        EnableWindow(GetDlgItem(controls,kLowerOutput),FALSE);
        UpdateStatus();
    }
    void Sliders() {
        exposure = static_cast<float>(static_cast<int>(SendMessageW(exposureSlider,TBM_GETPOS,0,0)))/10;
        white = static_cast<float>(SendMessageW(whiteSlider,TBM_GETPOS,0,0));
        wchar_t label[80];
        swprintf_s(label,L"Exposição: %.1f EV",exposure); SetWindowTextW(exposureLabel,label);
        swprintf_s(label,L"Branco ref.: %.0f nits",white); SetWindowTextW(whiteLabel,label);
    }
};
App app;
LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    try {
        if (hwnd==app.controls || hwnd==app.audioWindow) {
            switch (message) {
            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE: app.theme.Refresh(hwnd); break;
            case WM_ERASEBKGND:
                if (app.theme.Dark()) { app.theme.Erase(hwnd,reinterpret_cast<HDC>(wparam)); return TRUE; }
                break;
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORLISTBOX:
                if (app.theme.Dark()) return app.theme.ControlColor(reinterpret_cast<HDC>(wparam),message==WM_CTLCOLORLISTBOX);
                break;
            case WM_MEASUREITEM: {
                auto& item=*reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
                if (item.CtlType!=ODT_COMBOBOX) break;
                item.itemHeight=static_cast<UINT>(MulDiv(23,static_cast<int>(GetDpiForWindow(hwnd)),96));
                return TRUE;
            }
            case WM_DRAWITEM:
                app.theme.Draw(*reinterpret_cast<DRAWITEMSTRUCT const*>(lparam)); return TRUE;
            }
        }
        if (hwnd == app.controls) {
            switch (message) {
            case WM_COMMAND: app.Command(LOWORD(wparam),HIWORD(wparam)); return 0;
            case WM_HSCROLL: app.Sliders(); return 0;
            case WM_DISPLAYCHANGE: if (app.sourceBox) app.RefreshSources(); return 0;
            case kClosed: if (wparam == app.generation) app.Stop(); return 0;
            case WM_CLOSE: app.ConfirmClose(); return 0;
            case WM_TIMER:
                if (wparam==kCloseDialogTimer) { app.Tick(); return 0; }
                if (wparam==kWebStartupTimer) { if (!app.webActive) app.UseNativeUi(); return 0; }
                break;
            case WM_SYSCOMMAND:
                if (wparam==kSwitchUi) { app.ToggleUi(); return 0; }
                break;
            case WM_SHOWWINDOW:
                if (app.webActive || app.webPresenting) app.web.SetVisible(wparam!=0);
                break;
            case WM_MOVE: app.web.ParentMoved(); break;
            case WM_SIZE:
                if (wparam==SIZE_MINIMIZED) ShowWindow(hwnd,SW_HIDE);
                else app.ResizeWebUi();
                return 0;
            case kWebCommand: {
                std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> command(reinterpret_cast<wchar_t*>(lparam),CoTaskMemFree);
                if (wparam==1) app.UseNativeUi();
                else if (command) app.WebCommand(command.get());
                return 0;
            }
            case kTray:
                if (lparam == WM_LBUTTONUP || lparam == WM_RBUTTONUP) {
                    ShowWindow(hwnd,SW_RESTORE); SetForegroundWindow(hwnd);
                }
                return 0;
            case WM_DESTROY: {
                NOTIFYICONDATAW icon{sizeof(icon)}; icon.hWnd=hwnd; icon.uID=1;
                Shell_NotifyIconW(NIM_DELETE,&icon);
                PostQuitMessage(0); return 0;
            }
            }
        } else if (hwnd == app.audioWindow) {
            if (message==WM_COMMAND) { app.Command(LOWORD(wparam),HIWORD(wparam)); return 0; }
            if (message==WM_HSCROLL) { app.AudioGain(); return 0; }
            if (message==WM_CLOSE) { ShowWindow(hwnd,SW_HIDE); return 0; }
        } else if (hwnd == app.outputToolbar) {
            if (message==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
            if (message==WM_COMMAND) { app.Command(LOWORD(wparam),HIWORD(wparam)); return 0; }
            if (message==WM_LBUTTONDOWN) {
                POINT pointer{GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam)};
                ClientToScreen(hwnd,&pointer);
                app.HideOutputToolbar();
                return SendMessageW(app.output,WM_NCLBUTTONDOWN,HTCAPTION,MAKELPARAM(pointer.x,pointer.y));
            }
        } else if (hwnd == app.output) {
            if (message==WM_SYSCOMMAND && (wparam & 0xfff0)==SC_MINIMIZE && app.running) {
                app.LowerOutput(); return 0;
            }
            if (message==WM_ACTIVATE && LOWORD(wparam)!=WA_INACTIVE && app.outputLowered) {
                app.outputLowered=false;
                app.UpdateStatus();
            }
            if (message==WM_PAINT && app.gdiOutput) { app.PaintOutput(); return 0; }
            if (message==WM_ERASEBKGND && app.gdiOutput) return 1;
            if (message==WM_PRINTCLIENT && app.gdiOutput) {
                app.DrawGdiOutput(reinterpret_cast<HDC>(wparam)); return 0;
            }
            if (message==WM_KEYDOWN && wparam==VK_ESCAPE) { app.CloseOutput(); return 0; }
            if (message==WM_KEYDOWN && wparam==VK_F9) { app.LowerOutput(); return 0; }
            if (message == WM_CLOSE) { app.CloseOutput(); return 0; }
            if (message==WM_SIZE && app.running) {
                if (wparam==SIZE_RESTORED) {
                    auto extent=bridge::OutputExtent(app.resolution);
                    if (LOWORD(lparam)!=extent.width || HIWORD(lparam)!=extent.height) app.FitOutputWindow();
                }
                app.UpdateStatus();
            }
            if (message == WM_DPICHANGED) {
                auto suggested=reinterpret_cast<RECT*>(lparam);
                SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
                app.FitOutputWindow(); return 0;
            }
        }
    } catch (hresult_error const& error) { app.Fail(error); }
      catch (...) { app.Stop(); MessageBoxW(hwnd,L"Erro inesperado. Reinicie o app.",L"HDR Bridge",MB_ICONERROR); }
    return DefWindowProcW(hwnd,message,wparam,lparam);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    init_apartment(apartment_type::single_threaded);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    INITCOMMONCONTROLSEX init{sizeof(init),ICC_BAR_CLASSES}; InitCommonControlsEx(&init);
    try {
        if (!GraphicsCaptureSession::IsSupported()) {
            MessageBoxW(nullptr,L"Captura gráfica indisponível. Use Windows 11 com driver de vídeo atualizado.",L"HDR Bridge",MB_ICONERROR); return 1;
        }
        WNDCLASSW klass{}; klass.lpfnWndProc=WindowProc; klass.hInstance=instance;
        klass.hCursor=LoadCursorW(nullptr,IDC_ARROW); klass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
        klass.lpszClassName=L"HdrBridgeWindow";
        if (!RegisterClassW(&klass)) throw_last_error();
        WNDCLASSW outputClass=klass;
        outputClass.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        outputClass.lpszClassName=L"HdrBridgeOutput";
        if (!RegisterClassW(&outputClass)) throw_last_error();
        WNDCLASSW previewClass{};
        previewClass.lpfnWndProc=DefWindowProcW; previewClass.hInstance=instance;
        previewClass.hbrBackground=static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        previewClass.lpszClassName=L"HdrBridgePreview";
        if (!RegisterClassW(&previewClass)) throw_last_error();
        app.controls=CreateWindowW(klass.lpszClassName,L"HDR Bridge",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX|WS_CLIPCHILDREN,
            CW_USEDEFAULT,CW_USEDEFAULT,740,690,nullptr,nullptr,instance,nullptr);
        app.output=CreateWindowExW(WS_EX_APPWINDOW,outputClass.lpszClassName,L"HDR Bridge - Saida SDR",
            WS_POPUP,
            0,0,1280,720,nullptr,nullptr,instance,nullptr);
        if (!app.controls || !app.output) throw_last_error();
        app.frameTimer=CreateWaitableTimerExW(nullptr,nullptr,CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,TIMER_ALL_ACCESS);
        if (!app.frameTimer) app.frameTimer=CreateWaitableTimerExW(nullptr,nullptr,0,TIMER_ALL_ACCESS);
        if (!app.frameTimer) throw_last_error();
        app.InitGraphics(); app.InitUI(); app.FitOutputWindow(); ShowWindow(app.controls,SW_SHOW);
        app.UpdateUiMenu();
        if (!ReadSetting(L"NativeUi",1)) app.StartWebUi();
        bool quitting=false;
        while (!quitting) {
            DWORD event=MsgWaitForMultipleObjectsEx(1,&app.frameTimer,INFINITE,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
            if (event==WAIT_FAILED) throw_last_error();
            if (event==WAIT_OBJECT_0) {
                try { app.OnFrameTimer(); }
                catch (hresult_error const& error) { app.Fail(error); }
            }
            MSG message{};
            while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if (message.message==WM_QUIT) { quitting=true; break; }
                bool audioMessage=app.audioWindow && IsDialogMessageW(app.audioWindow,&message);
                if (!audioMessage && (app.webActive || !IsDialogMessageW(app.controls,&message))) {
                    TranslateMessage(&message); DispatchMessageW(&message);
                }
            }
        }
        app.Stop();
        app.web.Close();
        CloseHandle(app.frameTimer); app.frameTimer=nullptr;
        if (IsWindow(app.output)) DestroyWindow(app.output);
    } catch (hresult_error const& error) {
        MessageBoxW(nullptr,error.message().c_str(),L"HDR Bridge",MB_ICONERROR); return 1;
    }
    return 0;
}
