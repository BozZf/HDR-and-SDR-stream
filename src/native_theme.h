#pragma once
#include <windows.h>
#include <dwmapi.h>
#include <commctrl.h>
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

    COLORREF ControlSurface(bool selected) const {
        if (HighContrast()) return GetSysColor(selected ? COLOR_HIGHLIGHT : COLOR_WINDOW);
        if (Dark()) return selected ? RGB(76,79,88) : RGB(60,62,70);
        return selected ? RGB(207,216,232) : RGB(233,236,242);
    }

    LRESULT ControlColor(HDC dc,SurfaceRole role) const {
        bool dark=Dark();
        auto background=Surface(role!=SurfaceRole::Page);
        auto text=dark ? RGB(239,240,243) : RGB(33,37,46);
        if (HighContrast()) text=GetSysColor(COLOR_WINDOWTEXT);
        else {
            switch (role) {
            case SurfaceRole::List: background=ControlSurface(false); break;
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

    static std::wstring ControlText(HWND control,bool button,WPARAM index=0) {
        auto length=button ? GetWindowTextLengthW(control) : SendMessageW(control,CB_GETLBTEXTLEN,index,0);
        if (length<=0) return {};
        std::wstring label(static_cast<size_t>(length)+1,L'\0');
        if (button) GetWindowTextW(control,label.data(),static_cast<int>(label.size()));
        else SendMessageW(control,CB_GETLBTEXT,index,reinterpret_cast<LPARAM>(label.data()));
        return label;
    }

    static void DrawLabel(HWND control,HDC dc,RECT rect,COLORREF text,std::wstring const& label,UINT align) {
        auto oldFont=SelectObject(dc,reinterpret_cast<HFONT>(SendMessageW(control,WM_GETFONT,0,0)));
        SetBkMode(dc,TRANSPARENT);
        SetTextColor(dc,text);
        DrawTextW(dc,label.c_str(),-1,&rect,align|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS);
        if (oldFont) SelectObject(dc,oldFont);
    }

    COLORREF DrawControlFace(HWND control,HDC dc,RECT const& rect,bool active) const {
        auto parent=GetParent(control);
        auto brush=reinterpret_cast<HBRUSH>(SendMessageW(parent,WM_CTLCOLORSTATIC,
            reinterpret_cast<WPARAM>(dc),reinterpret_cast<LPARAM>(control)));
        FillRect(dc,&rect,brush);
        bool dark=Dark();
        bool enabled=IsWindowEnabled(control)!=FALSE;
        auto text=dark ? RGB(239,240,243) : RGB(33,37,46);
        if (!enabled) text=dark ? RGB(155,160,172) : RGB(110,118,133);
        auto border=enabled && (GetFocus()==control || active) ? (dark ? RGB(137,145,164) : RGB(104,121,151))
            : (dark ? RGB(82,87,101) : RGB(188,197,213));
        Rounded(dc,rect,ControlSurface(enabled && active),border,
            MulDiv(12,static_cast<int>(GetDpiForWindow(control)),96));
        return text;
    }

    void DrawButton(HWND control,HDC dc,bool hovered) const {
        RECT rect{};
        GetClientRect(control,&rect);
        auto state=SendMessageW(control,BM_GETSTATE,0,0);
        auto text=DrawControlFace(control,dc,rect,hovered || (state & BST_PUSHED)!=0);
        auto inset=MulDiv(8,static_cast<int>(GetDpiForWindow(control)),96);
        InflateRect(&rect,-inset,0);
        DrawLabel(control,dc,rect,text,ControlText(control,true),DT_CENTER|DT_HIDEPREFIX);
    }

    void DrawCombo(HWND control,HDC dc,bool hovered) const {
        RECT rect{};
        GetClientRect(control,&rect);
        bool open=SendMessageW(control,CB_GETDROPPEDSTATE,0,0)!=0;
        auto text=DrawControlFace(control,dc,rect,hovered || open);
        auto dpi=static_cast<int>(GetDpiForWindow(control));
        auto px=[dpi](int logical) { return MulDiv(logical,dpi,96); };
        int x=rect.right-px(14),y=(rect.top+rect.bottom)/2;
        POINT arrow[]={{x-px(4),y-px(2)},{x,y+px(2)},{x+px(4),y-px(2)}};
        auto oldPen=SelectObject(dc,GetStockObject(DC_PEN));
        auto oldColor=SetDCPenColor(dc,text);
        Polyline(dc,arrow,_countof(arrow));
        SetDCPenColor(dc,oldColor);
        SelectObject(dc,oldPen);
        rect.left+=px(10);
        rect.right-=px(27);
        auto index=SendMessageW(control,CB_GETCURSEL,0,0);
        if (index!=CB_ERR) DrawLabel(control,dc,rect,text,ControlText(control,false,index),DT_LEFT|DT_NOPREFIX);
    }

    void DrawListItem(DRAWITEMSTRUCT const& item) const {
        auto dark=Dark();
        bool selected=(item.itemState & (ODS_SELECTED|ODS_HOTLIGHT))!=0;
        bool disabled=(item.itemState & (ODS_DISABLED|ODS_GRAYED))!=0;
        auto bg=ControlSurface(selected);
        auto fg=dark ? (disabled ? RGB(155,160,172) : RGB(239,240,243))
            : disabled ? RGB(110,118,133) : RGB(33,37,46);
        if (HighContrast()) {
            fg=GetSysColor(disabled ? COLOR_GRAYTEXT : selected ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT);
        }
        auto dc=item.hDC;
        auto rect=item.rcItem;
        FillRect(dc,&rect,Brush(dc,bg));
        auto focus=rect;
        InflateRect(&focus,-3,-3);
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) DrawFocusRect(dc,&focus);

        InflateRect(&rect,-5,0);
        DrawLabel(item.hwndItem,dc,rect,fg,ControlText(item.hwndItem,false,item.itemID),DT_LEFT|DT_NOPREFIX);
    }

    void AttachControl(HWND control,bool combo) {
        SetWindowSubclass(control,ControlProc,combo ? kCombo : kButton,reinterpret_cast<DWORD_PTR>(this));
    }
private:
    static constexpr UINT_PTR kButton=1,kCombo=2;
    static LRESULT CALLBACK ControlProc(HWND window,UINT message,WPARAM wparam,LPARAM lparam,
                                        UINT_PTR kind,DWORD_PTR context) {
        auto const& theme=*reinterpret_cast<NativeTheme const*>(context);
        if ((message==WM_PAINT || message==WM_PRINTCLIENT || message==WM_ERASEBKGND) && !HighContrast()) {
            if (message==WM_ERASEBKGND) return TRUE;
            PAINTSTRUCT paint{};
            HDC dc=message==WM_PAINT ? BeginPaint(window,&paint) : reinterpret_cast<HDC>(wparam);
            POINT cursor{};
            bool hovered=GetCursorPos(&cursor) && WindowFromPoint(cursor)==window;
            if (kind==kCombo) theme.DrawCombo(window,dc,hovered);
            else theme.DrawButton(window,dc,hovered);
            if (message==WM_PAINT) EndPaint(window,&paint);
            return 0;
        }

        auto result=DefSubclassProc(window,message,wparam,lparam);
        switch (message) {
        case WM_MOUSEMOVE: {
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,window,0};
            TrackMouseEvent(&track);
            InvalidateRect(window,nullptr,FALSE);
            break;
        }
        case WM_MOUSELEAVE: case WM_ENABLE: case WM_SETTEXT:
        case WM_SETFOCUS: case WM_KILLFOCUS: case WM_CAPTURECHANGED:
        case WM_LBUTTONDOWN: case WM_LBUTTONUP: case WM_KEYDOWN: case WM_KEYUP:
        case BM_SETSTATE: case CB_SETCURSEL: case CB_RESETCONTENT: case CB_SHOWDROPDOWN:
            InvalidateRect(window,nullptr,FALSE);
            break;
        }
        return result;
    }

    std::map<std::tuple<UINT,int,bool>,HFONT> fonts_;
};
}
