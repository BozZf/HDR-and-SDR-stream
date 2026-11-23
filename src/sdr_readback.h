#pragma once
#include <d3d11.h>
#include <winrt/base.h>
#include <vector>
#include <cstring>

namespace bridge {
// Explicit compatibility path only: GPU SDR frame -> top-down BGRA pixels for
// ordinary GDI window drawing. Kept out of the normal GPU presentation path.
class SdrReadback {
public:
    void Clear() { pixels_.clear(); width_=height_=0; staging_=nullptr; }
    bool Update(ID3D11Device* device,ID3D11DeviceContext* context,ID3D11Texture2D* source) {
        D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
        if (desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM || desc.SampleDesc.Count!=1)
            throw winrt::hresult_error(E_INVALIDARG,L"A saída GDI aceita somente SDR BGRA8.");
        bool changed=!staging_ || desc.Width!=width_ || desc.Height!=height_;
        if (changed) {
            staging_=nullptr;
            desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0;
            desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
            winrt::check_hresult(device->CreateTexture2D(&desc,nullptr,staging_.put()));
            pixels_.resize(static_cast<size_t>(desc.Width)*desc.Height*4);
            width_=desc.Width; height_=desc.Height;
        }
        context->CopyResource(staging_.get(),source);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        winrt::check_hresult(context->Map(staging_.get(),0,D3D11_MAP_READ,0,&mapped));
        size_t stride=static_cast<size_t>(width_)*4;
        for (UINT y=0;y<height_;++y) {
            auto row=static_cast<unsigned char const*>(mapped.pData)+static_cast<size_t>(y)*mapped.RowPitch;
            auto cached=pixels_.data()+y*stride;
            if (changed || std::memcmp(cached,row,stride)!=0) {
                std::memcpy(cached,row,stride);
                changed=true;
            }
        }
        context->Unmap(staging_.get(),0);
        return changed;
    }
    void Draw(HDC dc,RECT const& client) const {
        if (pixels_.empty()) {
            FillRect(dc,&client,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            return;
        }
        // One draw covers the client area. An intermediate clear can be seen by
        // the compositor or another capturer between GDI operations.
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth=static_cast<LONG>(width_);
        bitmap.bmiHeader.biHeight=-static_cast<LONG>(height_);
        bitmap.bmiHeader.biPlanes=1;
        bitmap.bmiHeader.biBitCount=32;
        bitmap.bmiHeader.biCompression=BI_RGB;
        StretchDIBits(dc,client.left,client.top,client.right-client.left,client.bottom-client.top,
            0,0,static_cast<int>(width_),static_cast<int>(height_),pixels_.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);
    }
    unsigned Width() const { return width_; }
    unsigned Height() const { return height_; }
    std::vector<unsigned char> const& Pixels() const { return pixels_; }
private:
    winrt::com_ptr<ID3D11Texture2D> staging_;
    std::vector<unsigned char> pixels_;
    unsigned width_=0,height_=0;
};
}
