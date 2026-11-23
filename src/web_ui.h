#pragma once
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
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

// The host accepts only messages posted by its embedded document.
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
                    if (FAILED(result) || !environment || host_!=host || !IsWindow(host)) return S_OK;
                    return environment->CreateCoreWebView2Controller(host,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this,host,owner,commandMessage](HRESULT result,ICoreWebView2Controller* controller) -> HRESULT {
                                if (FAILED(result) || !controller || host_!=host || !IsWindow(host)) return S_OK;
                                try { Initialize(controller,owner,commandMessage); }
                                catch (...) { Close(); }
                                return S_OK;
                            }).Get());
                }).Get());
    }
    bool Resize() const {
        RECT bounds{};
        return controller_ && GetClientRect(host_,&bounds) && bounds.right>0 && bounds.bottom>0 &&
            SUCCEEDED(controller_->put_Bounds(bounds));
    }
    bool SetVisible(bool visible) const {
        return controller_ && SUCCEEDED(controller_->put_IsVisible(visible ? TRUE : FALSE));
    }
    void ParentMoved() const {
        if (controller_) controller_->NotifyParentWindowPositionChanged();
    }
    bool Present() const {
        if (!IsWindowVisible(host_) || !Resize()) return false;
        ParentMoved();
        return SetVisible(true) && SUCCEEDED(web_->PostWebMessageAsJson(L"{\"type\":\"present\"}"));
    }
    void Send(std::wstring const& message) const {
        if (web_) web_->PostWebMessageAsJson(message.c_str());
    }
    void SetBackground(COREWEBVIEW2_COLOR color) const {
        if (!controller_) return;
        if (auto background=controller_.try_as<ICoreWebView2Controller2>())
            background->put_DefaultBackgroundColor(color);
    }
    void Close() {
        if (controller_) controller_->Close();
        web_=nullptr;
        controller_=nullptr;
        host_=nullptr;
    }
private:
    static constexpr wchar_t kPageUri[]=L"https://hdr-bridge.invalid/index.html";

    void Initialize(ICoreWebView2Controller* controller, HWND owner, UINT commandMessage) {
        using Microsoft::WRL::Callback;
        using winrt::check_hresult;
        controller_.copy_from(controller);
        check_hresult(controller_->get_CoreWebView2(web_.put()));
        check_hresult(controller_->put_IsVisible(FALSE));
        winrt::com_ptr<ICoreWebView2Settings> settings;
        check_hresult(web_->get_Settings(settings.put()));
        check_hresult(settings->put_AreHostObjectsAllowed(FALSE));
        check_hresult(settings->put_AreDevToolsEnabled(FALSE));
        check_hresult(settings->put_AreDefaultContextMenusEnabled(FALSE));
        check_hresult(settings->put_AreDefaultScriptDialogsEnabled(FALSE));
        check_hresult(settings->put_IsStatusBarEnabled(FALSE));
        check_hresult(settings->put_IsZoomControlEnabled(FALSE));
        EventRegistrationToken token{};
        // Serve the exact document from the executable. A fixed URI also gives
        // navigation and web-message events the same origin to validate.
        check_hresult(web_->AddWebResourceRequestedFilter(kPageUri,COREWEBVIEW2_WEB_RESOURCE_CONTEXT_DOCUMENT));
        auto environmentSource=web_.as<ICoreWebView2_2>();
        winrt::com_ptr<ICoreWebView2Environment> environment;
        check_hresult(environmentSource->get_Environment(environment.put()));
        check_hresult(web_->add_WebResourceRequested(
            Callback<ICoreWebView2WebResourceRequestedEventHandler>(
                [environment](ICoreWebView2*,ICoreWebView2WebResourceRequestedEventArgs* event) -> HRESULT {
                    winrt::com_ptr<IStream> page;
                    page.attach(SHCreateMemStream(reinterpret_cast<BYTE const*>(kUiHtml),static_cast<UINT>(sizeof(kUiHtml)-1)));
                    winrt::com_ptr<ICoreWebView2WebResourceResponse> response;
                    auto hr=environment->CreateWebResourceResponse(page.get(),page ? 200 : 500,
                        page ? L"OK" : L"Internal Server Error",L"Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store",response.put());
                    return SUCCEEDED(hr) ? event->put_Response(response.get()) : hr;
                }).Get(),&token));
        check_hresult(web_->add_NavigationStarting(
            Callback<ICoreWebView2NavigationStartingEventHandler>(
                [](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs* event) -> HRESULT {
                    PWSTR uri=nullptr;
                    bool allowed=SUCCEEDED(event->get_Uri(&uri)) && uri && std::wstring_view(uri)==kPageUri;
                    CoTaskMemFree(uri);
                    return event->put_Cancel(!allowed);
                }).Get(),&token));
        check_hresult(web_->add_NewWindowRequested(
            Callback<ICoreWebView2NewWindowRequestedEventHandler>(
                [](ICoreWebView2*,ICoreWebView2NewWindowRequestedEventArgs* event) -> HRESULT {
                    return event->put_Handled(TRUE);
                }).Get(),&token));
        check_hresult(web_->add_ProcessFailed(
            Callback<ICoreWebView2ProcessFailedEventHandler>(
                [owner,commandMessage](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs* event) -> HRESULT {
                    COREWEBVIEW2_PROCESS_FAILED_KIND kind{};
                    if (SUCCEEDED(event->get_ProcessFailedKind(&kind)) &&
                        (kind==COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED ||
                         kind==COREWEBVIEW2_PROCESS_FAILED_KIND_RENDER_PROCESS_EXITED)) {
                        PostMessageW(owner,commandMessage,1,0);
                    }
                    return S_OK;
                }).Get(),&token));
        check_hresult(web_->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [owner,commandMessage](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs* event) -> HRESULT {
                    PWSTR uri=nullptr;
                    bool allowed=SUCCEEDED(event->get_Source(&uri)) && uri && std::wstring_view(uri)==kPageUri;
                    CoTaskMemFree(uri);
                    PWSTR message=nullptr;
                    if (allowed && SUCCEEDED(event->TryGetWebMessageAsString(&message)) && message) {
                        if (wcsnlen_s(message,129)>128 || !PostMessageW(owner,commandMessage,0,reinterpret_cast<LPARAM>(message)))
                            CoTaskMemFree(message);
                    }
                    return S_OK;
                }).Get(),&token));
        Resize();
        check_hresult(web_->Navigate(kPageUri));
    }
    HWND host_{};
    winrt::com_ptr<ICoreWebView2Controller> controller_;
    winrt::com_ptr<ICoreWebView2> web_;
};
}
