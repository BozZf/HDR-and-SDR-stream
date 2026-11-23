#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <map>
#include <string>
#include <tuple>

namespace bridge {
enum class Theme : DWORD { Dark, Light };
enum class SurfaceRole { Page, Panel, List, Preview, Notice, Active, Muted };

class NativeTheme {
public:
    Theme choice=Theme::Dark;
    ~NativeTheme() {
        for (auto const& [key,font] : fonts_) DeleteObject(font);
    }

    static bool HighContrast() {
        HIGHCONTRASTW contrast{sizeof(contrast)};
        return SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0) &&
            (contrast.dwFlags & HCF_HIGHCONTRASTON);
    }
    bool Dark() const { return choice==Theme::Dark && !HighContrast(); }

    HFONT Font(UINT dpi,int pixels,bool heading) {
        auto key=std::tuple{dpi,pixels,heading};
        if (auto existing=fonts_.find(key);existing!=fonts_.end()) return existing->second;
        auto font=CreateFontW(-MulDiv(pixels,static_cast<int>(dpi),96),0,0,0,heading ? FW_SEMIBOLD : FW_NORMAL,
            FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
            DEFAULT_PITCH,L"Segoe UI");
        if (font) fonts_.emplace(key,font);
        return font ? font : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
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

    COLORREF Surface(bool panel) const {
        if (HighContrast()) return GetSysColor(COLOR_WINDOW);
        return Dark() ? (panel ? RGB(39,42,49) : RGB(30,32,37))
            : (panel ? RGB(252,252,254) : RGB(239,241,245));
    }

    static void Rounded(HDC dc,RECT const& rect,COLORREF fill,COLORREF stroke,int radius) {
        auto oldBrush=SelectObject(dc,Brush(dc,fill));
        auto oldPen=SelectObject(dc,GetStockObject(DC_PEN));
        auto oldStroke=SetDCPenColor(dc,stroke);
        RoundRect(dc,rect.left,rect.top,rect.right,rect.bottom,radius,radius);
        SetDCPenColor(dc,oldStroke);
        SelectObject(dc,oldPen);
        SelectObject(dc,oldBrush);
    }

    void Card(HDC dc,RECT const& rect,int radius) const {
        Rounded(dc,rect,Surface(true),HighContrast() ? GetSysColor(COLOR_WINDOWTEXT) :
            Dark() ? RGB(57,61,71) : RGB(215,220,229),radius);
    }

    LRESULT ControlColor(HDC dc,SurfaceRole role) const {
        bool dark=Dark();
        auto background=Surface(role!=SurfaceRole::Page);
        auto text=dark ? RGB(239,240,243) : RGB(33,37,46);
        if (HighContrast()) text=GetSysColor(COLOR_WINDOWTEXT);
        else {
            switch (role) {
            case SurfaceRole::List: background=dark ? RGB(49,52,61) : RGB(233,236,242); break;
            case SurfaceRole::Preview:
                background=dark ? RGB(22,24,29) : RGB(226,230,239);
                text=dark ? RGB(175,182,197) : RGB(76,87,108);
                break;
            case SurfaceRole::Notice: text=dark ? RGB(234,210,170) : RGB(119,74,23); break;
            case SurfaceRole::Active: text=dark ? RGB(173,215,194) : RGB(35,103,75); break;
            case SurfaceRole::Muted: text=dark ? RGB(185,191,204) : RGB(75,83,102); break;
            default: break;
            }
        }
        SetTextColor(dc,text);
        SetBkColor(dc,background);
        if (role!=SurfaceRole::List && role!=SurfaceRole::Preview) SetBkMode(dc,TRANSPARENT);
        return reinterpret_cast<LRESULT>(Brush(dc,background));
    }

    void Erase(HWND window,HDC dc) const {
        RECT client{};
        GetClientRect(window,&client);
        FillRect(dc,&client,Brush(dc,Surface(false)));
    }

    void Draw(DRAWITEMSTRUCT const& item,bool primary,bool panel) const {
        auto dark=Dark();
        bool selected=(item.itemState & (ODS_SELECTED|ODS_HOTLIGHT))!=0;
        bool disabled=(item.itemState & (ODS_DISABLED|ODS_GRAYED))!=0;
        bool button=item.CtlType==ODT_BUTTON;
        auto bg=dark ? (selected ? RGB(76,82,96) : primary && !disabled ? RGB(65,71,84) : RGB(49,52,61))
            : selected ? RGB(207,216,232) : primary && !disabled ? RGB(216,224,239) : RGB(233,236,242);
        auto fg=dark ? (disabled ? RGB(155,160,172) : RGB(239,240,243))
            : disabled ? RGB(110,118,133) : RGB(33,37,46);
        if (HighContrast()) {
            bg=GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
            fg=GetSysColor(disabled ? COLOR_GRAYTEXT : selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
        }
        auto dc=item.hDC;
        auto rect=item.rcItem;
        if (button) {
            FillRect(dc,&rect,Brush(dc,Surface(panel)));
            auto border=HighContrast() ? fg : dark ? RGB(82,87,101) : RGB(188,197,213);
            Rounded(dc,rect,bg,border,MulDiv(13,static_cast<int>(GetDpiForWindow(item.hwndItem)),96));
        } else FillRect(dc,&rect,Brush(dc,bg));
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
private:
    std::map<std::tuple<UINT,int,bool>,HFONT> fonts_;
};
}
