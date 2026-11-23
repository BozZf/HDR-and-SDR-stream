#pragma once
#include <windows.h>
#include <shlobj.h>
#include <winrt/base.h>
#include <wrl.h>
#include <WebView2.h>
#include <string>
#include <string_view>
#include <memory>
#include "ui_source.h"

namespace bridge {
inline std::wstring JsonString(std::wstring_view value) {
    std::wstring quoted=L"\"";
    for (wchar_t c : value) {
        switch (c) {
        case L'"': quoted+=L"\\\""; break;
        case L'\\': quoted+=L"\\\\"; break;
        case L'\r': quoted+=L"\\r"; break;
        case L'\n': quoted+=L"\\n"; break;
        case L'\t': quoted+=L"\\t"; break;
        default:
            if (c>=32) quoted+=c;
            else quoted+=L' ';
        }
    }
    return quoted+L'"';
}

// The host accepts only messages posted by its embedded about:blank document.
// Application-level handlers validate command names, arity and value ranges.
class WebUi {
public:
    WebUi()=default;
    WebUi(WebUi const&)=delete;
    WebUi& operator=(WebUi const&)=delete;
    void Start(HWND host, HWND owner, UINT commandMessage) {
        using Microsoft::WRL::Callback;
        host_=host;
        PWSTR localPath=nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&localPath))) return;
        std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> ownedPath(localPath,CoTaskMemFree);
        auto profile=std::wstring(localPath)+L"\\HDR Bridge\\WebView2";
        CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.c_str(),nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this,host,owner,commandMessage](HRESULT result,ICoreWebView2Environment* environment) -> HRESULT {
                    if (FAILED(result) || !environment || !IsWindow(host)) return S_OK;
                    return environment->CreateCoreWebView2Controller(host,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this,host,owner,commandMessage](HRESULT result,ICoreWebView2Controller* controller) -> HRESULT {
                                if (FAILED(result) || !controller || !IsWindow(host)) return S_OK;
                                try { Initialize(controller,owner,commandMessage); }
                                catch (...) { Close(); }
                                return S_OK;
                            }).Get());
                }).Get());
    }
    void Resize() const {
        if (!controller_) return;
        RECT bounds{};
        if (GetClientRect(host_,&bounds)) controller_->put_Bounds(bounds);
    }
    void Send(std::wstring const& message) const {
        if (web_) web_->PostWebMessageAsJson(message.c_str());
    }
    void Close() {
        if (controller_) controller_->Close();
        web_=nullptr;
        controller_=nullptr;
    }
private:
    void Initialize(ICoreWebView2Controller* controller, HWND owner, UINT commandMessage) {
        using Microsoft::WRL::Callback;
        using winrt::check_hresult;
        controller_.copy_from(controller);
        check_hresult(controller_->get_CoreWebView2(web_.put()));
        winrt::com_ptr<ICoreWebView2Settings> settings;
        check_hresult(web_->get_Settings(settings.put()));
        check_hresult(settings->put_AreHostObjectsAllowed(FALSE));
        check_hresult(settings->put_AreDevToolsEnabled(FALSE));
        check_hresult(settings->put_AreDefaultContextMenusEnabled(FALSE));
        check_hresult(settings->put_AreDefaultScriptDialogsEnabled(FALSE));
        check_hresult(settings->put_IsStatusBarEnabled(FALSE));
        check_hresult(settings->put_IsZoomControlEnabled(FALSE));
        EventRegistrationToken token{};
        check_hresult(web_->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* event) -> HRESULT {
                    PWSTR uri=nullptr;
                    bool allowed=SUCCEEDED(event->get_Uri(&uri)) && uri && std::wstring_view(uri)==L"about:blank";
                    CoTaskMemFree(uri);
                    return event->put_Cancel(!allowed);
                }).Get(),&token));
        check_hresult(web_->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* event) -> HRESULT {
                    return event->put_Handled(TRUE);
                }).Get(),&token));
        check_hresult(web_->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [owner,commandMessage](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* event) -> HRESULT {
                    PWSTR uri=nullptr;
                    bool allowed=SUCCEEDED(event->get_Source(&uri)) && uri && std::wstring_view(uri)==L"about:blank";
                    CoTaskMemFree(uri);
                    PWSTR message=nullptr;
                    if (allowed && SUCCEEDED(event->TryGetWebMessageAsString(&message)) && message) {
                        if (wcsnlen_s(message,129)>128 || !PostMessageW(owner,commandMessage,0,reinterpret_cast<LPARAM>(message)))
                            CoTaskMemFree(message);
                    }
                    return S_OK;
                }).Get(),&token));
        Resize();
        auto html=winrt::to_hstring(kUiHtml);
        check_hresult(web_->NavigateToString(html.c_str()));
    }
    HWND host_{};
    winrt::com_ptr<ICoreWebView2Controller> controller_;
    winrt::com_ptr<ICoreWebView2> web_;
};
}
