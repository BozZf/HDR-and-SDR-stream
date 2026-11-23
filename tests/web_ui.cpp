#include "web_ui.h"
#include <iostream>

namespace {
constexpr UINT kWebCommand=WM_APP+1;
constexpr int kSkip=77;
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
    auto window=CreateWindowW(L"STATIC",L"HDR Bridge WebView2 test",WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,CW_USEDEFAULT,930,615,nullptr,nullptr,instance,nullptr);
    if (!window) return 1;
    // Production keeps the host hidden while it waits for the page's ready message.
    auto host=CreateWindowW(L"STATIC",L"",WS_CHILD,0,0,900,580,window,nullptr,instance,nullptr);
    if (!host) return 1;
    ShowWindow(window,SW_SHOWNA);
    bridge::WebUi web;
    web.Start(host,window,kWebCommand);

    bool ready=false;
    bool failed=false;
    auto deadline=GetTickCount64()+30000;
    while (!ready && !failed && GetTickCount64()<deadline) {
        MsgWaitForMultipleObjectsEx(0,nullptr,100,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
            if (message.message==kWebCommand && message.hwnd==window) {
                std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> text(
                    reinterpret_cast<wchar_t*>(message.lParam),CoTaskMemFree);
                if (message.wParam==1) failed=true;
                else if (text && std::wstring_view(text.get())==L"ready") ready=true;
            } else {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }
    web.Close();
    DestroyWindow(window);
    if (ready && !failed) return 0;
    std::cerr << "The embedded WebView2 page did not initialize.\n";
    return 1;
}
