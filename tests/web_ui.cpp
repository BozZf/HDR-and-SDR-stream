#include "web_ui.h"
#include <dwmapi.h>
#include <iostream>

namespace {
constexpr UINT kWebCommand=WM_APP+1;
constexpr int kSkip=77;

bool ControlsPainted(HWND host) {
    // Check the visible screen, not just the contents of the WebView's document.
    // These points are inside the page background and the first panel's padding.
    int dpi=static_cast<int>(GetDpiForWindow(host));
    POINT background{MulDiv(8,dpi,96),MulDiv(8,dpi,96)};
    POINT panel{MulDiv(37,dpi,96),MulDiv(112,dpi,96)};
    if (!ClientToScreen(host,&background) || !ClientToScreen(host,&panel)) return false;
    DwmFlush();
    auto dc=GetDC(nullptr);
    if (!dc) return false;
    auto first=GetPixel(dc,background.x,background.y);
    auto second=GetPixel(dc,panel.x,panel.y);
    ReleaseDC(nullptr,dc);
    return first==RGB(30,32,37) && second==RGB(39,42,49);
}
}

int main() {
    winrt::init_apartment(winrt::apartment_type::single_threaded);
    PWSTR version=nullptr;
    auto runtime=GetAvailableCoreWebView2BrowserVersionString(nullptr,&version);
    CoTaskMemFree(version);
    if (runtime==HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND)) {
        std::cout << "WebView2 Runtime is unavailable.\n";
        return kSkip;
    }
    if (FAILED(runtime)) return 1;

    auto instance=GetModuleHandleW(nullptr);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    auto window=CreateWindowExW(WS_EX_TOPMOST,L"STATIC",L"HDR Bridge WebView2 test",
        WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,20,20,750,600,nullptr,nullptr,instance,nullptr);
    if (!window) return 1;
    // Production keeps the host hidden while it waits for the page's ready message.
    RECT area{};
    GetClientRect(window,&area);
    auto host=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_CLIPCHILDREN|WS_CLIPSIBLINGS,
        0,0,area.right,area.bottom,window,nullptr,instance,nullptr);
    auto fallback=CreateWindowW(L"BUTTON",L"Controles nativos",WS_CHILD|WS_VISIBLE,
        10,15,160,32,window,nullptr,instance,nullptr);
    if (!host || !fallback) return 1;
    ShowWindow(window,SW_SHOWNA);
    bridge::WebUi web;
    web.Start(host,window,kWebCommand);

    bool presented=false;
    bool painted=false;
    bool failed=false;
    auto deadline=GetTickCount64()+30000;
    while (!painted && !failed && GetTickCount64()<deadline) {
        MsgWaitForMultipleObjectsEx(0,nullptr,100,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if (message.message==kWebCommand && message.hwnd==window) {
                std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> text(
                    reinterpret_cast<wchar_t*>(message.lParam),CoTaskMemFree);
                if (message.wParam==1) failed=true;
                else if (text && std::wstring_view(text.get())==L"ready") {
                    SetWindowPos(host,HWND_BOTTOM,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_SHOWWINDOW);
                    failed=!IsWindowVisible(fallback) || !web.Present();
                } else if (text && std::wstring_view(text.get())==L"presented") {
                    SetWindowPos(host,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
                    ShowWindow(fallback,SW_HIDE);
                    presented=true;
                }
            } else {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        if (presented) painted=ControlsPainted(host);
    }
    web.Close();
    DestroyWindow(window);
    if (painted && !failed) return 0;
    std::cerr << (presented ? "The embedded controls did not paint on screen.\n" :
        "The embedded page did not confirm its visible layout.\n");
    return 1;
}
