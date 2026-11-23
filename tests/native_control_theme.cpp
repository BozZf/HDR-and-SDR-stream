#include "native_theme.h"
#include <iostream>

namespace {
LRESULT CALLBACK ThemeParent(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if (message==WM_CTLCOLORSTATIC) {
        auto theme=reinterpret_cast<bridge::NativeTheme const*>(GetWindowLongPtrW(window,GWLP_USERDATA));
        return theme->ControlColor(reinterpret_cast<HDC>(wparam),bridge::SurfaceRole::Panel);
    }
    return DefWindowProcW(window,message,wparam,lparam);
}

bool HasFill(HWND control,HDC dc,COLORREF expected) {
    PatBlt(dc,0,0,160,48,WHITENESS);
    SendMessageW(control,WM_PRINTCLIENT,reinterpret_cast<WPARAM>(dc),PRF_CLIENT);
    return GetPixel(dc,8,8)==expected;
}
}

int main() {
    if (bridge::NativeTheme::HighContrast()) return 77;
    INITCOMMONCONTROLSEX commonControls{sizeof(commonControls),ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&commonControls);
    bridge::NativeTheme theme;
    auto instance=GetModuleHandleW(nullptr);
    WNDCLASSW parentClass{};
    parentClass.hInstance=instance;
    parentClass.lpszClassName=L"HdrBridgeThemeTest";
    parentClass.lpfnWndProc=ThemeParent;
    if (!RegisterClassW(&parentClass)) return 1;
    auto parent=CreateWindowW(parentClass.lpszClassName,L"",WS_OVERLAPPEDWINDOW,
        0,0,360,180,nullptr,nullptr,instance,nullptr);
    if (!parent) return 1;
    SetWindowLongPtrW(parent,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(&theme));
    auto button=CreateWindowW(WC_BUTTONW,L"Recolher",WS_CHILD|WS_VISIBLE,0,0,140,32,parent,nullptr,instance,nullptr);
    auto combo=CreateWindowW(WC_COMBOBOXW,L"",WS_CHILD|WS_VISIBLE|CBS_DROPDOWNLIST,
        0,42,140,100,parent,nullptr,instance,nullptr);
    if (!button || !combo) return 1;
    theme.AttachControl(button,false);
    theme.AttachControl(combo,true);
    SendMessageW(combo,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(L"1080p"));
    SendMessageW(combo,CB_SETCURSEL,0,0);

    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize=sizeof(bitmap.bmiHeader);
    bitmap.bmiHeader.biWidth=160;
    bitmap.bmiHeader.biHeight=-48;
    bitmap.bmiHeader.biPlanes=1;
    bitmap.bmiHeader.biBitCount=32;
    bitmap.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    auto image=CreateDIBSection(nullptr,&bitmap,DIB_RGB_COLORS,&bits,nullptr,0);
    auto dc=CreateCompatibleDC(nullptr);
    if (!image || !dc) return 1;
    auto previous=SelectObject(dc,image);

    auto matches=[&](bridge::Theme choice,COLORREF normal,COLORREF pressed) {
        theme.choice=choice;
        auto restMatches=HasFill(button,dc,normal) && HasFill(combo,dc,normal);
        SendMessageW(button,BM_SETSTATE,TRUE,0);
        auto pressMatches=HasFill(button,dc,pressed);
        SendMessageW(button,BM_SETSTATE,FALSE,0);
        return restMatches && pressMatches && HasFill(button,dc,normal);
    };
    auto dark=matches(bridge::Theme::Dark,RGB(60,62,70),RGB(76,79,88));
    auto light=matches(bridge::Theme::Light,RGB(233,236,242),RGB(207,216,232));
    SelectObject(dc,previous);
    DeleteDC(dc);
    DeleteObject(image);
    DestroyWindow(parent);
    if (dark && light) {
        std::cout << "Buttons and selectors paint the same fill in both themes.\n";
        return 0;
    }
    std::cerr << "Control colors did not match (dark=" << dark << ", light=" << light << ").\n";
    return 1;
}
