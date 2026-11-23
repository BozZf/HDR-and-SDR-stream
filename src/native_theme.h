#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <string>

namespace bridge {
enum class Theme : DWORD { Dark, Light };

class NativeTheme {
public:
    Theme choice=Theme::Dark;

    bool Dark() const {
        HIGHCONTRASTW contrast{sizeof(contrast)};
        return choice==Theme::Dark &&
            (!SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) ||
             !(contrast.dwFlags & HCF_HIGHCONTRASTON));
    }

    void Refresh(HWND window) const {
        if (!window) return;
        auto dark=Dark();
        COLORREF caption=dark ? RGB(39,42,49) : DWMWA_COLOR_DEFAULT;
        COLORREF text=dark ? RGB(239,240,243) : DWMWA_COLOR_DEFAULT;
        DwmSetWindowAttribute(window,DWMWA_CAPTION_COLOR,&caption,sizeof(caption));
        DwmSetWindowAttribute(window,DWMWA_TEXT_COLOR,&text,sizeof(text));
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN);
    }

    static HBRUSH Brush(HDC dc,COLORREF color) {
        SetDCBrushColor(dc,color);
        return static_cast<HBRUSH>(GetStockObject(DC_BRUSH));
    }

    LRESULT ControlColor(HDC dc,bool list) const {
        auto background=list ? RGB(49,52,61) : RGB(30,32,37);
        SetTextColor(dc,RGB(239,240,243));
        SetBkColor(dc,background);
        return reinterpret_cast<LRESULT>(Brush(dc,background));
    }

    static void Erase(HWND window,HDC dc) {
        RECT client{};
        GetClientRect(window,&client);
        FillRect(dc,&client,Brush(dc,RGB(30,32,37)));
    }

    void Draw(DRAWITEMSTRUCT const& item) const {
        auto dark=Dark();
        bool selected=(item.itemState & (ODS_SELECTED|ODS_HOTLIGHT))!=0;
        bool disabled=(item.itemState & (ODS_DISABLED|ODS_GRAYED))!=0;
        bool button=item.CtlType==ODT_BUTTON;
        auto bg=dark ? (selected ? RGB(68,73,85) : RGB(49,52,61))
            : GetSysColor(button ? COLOR_BTNFACE : selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
        auto fg=dark ? (disabled ? RGB(155,160,172) : RGB(239,240,243))
            : GetSysColor(disabled ? COLOR_GRAYTEXT : button ? COLOR_BTNTEXT :
                selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
        auto dc=item.hDC;
        auto rect=item.rcItem;
        FillRect(dc,&rect,Brush(dc,bg));
        if (button) {
            FrameRect(dc,&rect,Brush(dc,dark ? RGB(87,91,103) :
                GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_BTNSHADOW)));
        }
        auto focus=rect;
        InflateRect(&focus,-3,-3);
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) DrawFocusRect(dc,&focus);

        auto length=button ? GetWindowTextLengthW(item.hwndItem) :
            SendMessageW(item.hwndItem,CB_GETLBTEXTLEN,item.itemID,0);
        if (length<=0) return;
        std::wstring label(static_cast<size_t>(length)+1,L'\0');
        if (button) GetWindowTextW(item.hwndItem,label.data(),static_cast<int>(label.size()));
        else SendMessageW(item.hwndItem,CB_GETLBTEXT,item.itemID,reinterpret_cast<LPARAM>(label.data()));
        auto oldFont=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(item.hwndItem,WM_GETFONT,0,0)));
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,fg);
        InflateRect(&rect,-5,0);
        UINT align=button ? DT_CENTER : DT_LEFT|DT_NOPREFIX;
        if (item.itemState & ODS_NOACCEL) align|=DT_HIDEPREFIX;
        DrawTextW(dc,label.c_str(),-1,&rect,align|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        if (oldFont) SelectObject(dc,oldFont);
    }
};
}
