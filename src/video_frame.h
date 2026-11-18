#pragma once
#include <d3d11.h>
#include <winrt/base.h>
#include "stream_settings.h"

namespace bridge {
// The program frame has an explicit size. Neither preview HWND dimensions nor
// visibility participate in allocating or processing this texture.
class VideoFrame {
public:
    void Reset() {
        view_=nullptr; target_=nullptr; texture_=nullptr; extent_={};
    }
    void Ensure(ID3D11Device* device, Extent size, bool hdr) {
        auto format=hdr ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_B8G8R8A8_UNORM;
        if (texture_ && extent_.width==size.width && extent_.height==size.height && format_==format) return;
        Reset();
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=size.width; desc.Height=size.height;
        desc.MipLevels=desc.ArraySize=1; desc.SampleDesc.Count=1;
        desc.Format=format; desc.Usage=D3D11_USAGE_DEFAULT;
        desc.BindFlags=D3D11_BIND_RENDER_TARGET|D3D11_BIND_SHADER_RESOURCE;
        winrt::check_hresult(device->CreateTexture2D(&desc,nullptr,texture_.put()));
        winrt::check_hresult(device->CreateRenderTargetView(texture_.get(),nullptr,target_.put()));
        winrt::check_hresult(device->CreateShaderResourceView(texture_.get(),nullptr,view_.put()));
        extent_=size; format_=format;
    }
    void Process(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source,
        Extent sourceSize, ID3D11VertexShader* vertex, ID3D11PixelShader* color,
        ID3D11SamplerState* sampler, ID3D11Buffer* settings) {
        Draw(context,source,sourceSize,target_.get(),extent_,vertex,color,sampler,settings);
    }
    void Blit(ID3D11DeviceContext* context, ID3D11RenderTargetView* destination,
        Extent destinationSize, ID3D11VertexShader* vertex, ID3D11PixelShader* display,
        ID3D11SamplerState* sampler) const {
        Draw(context,view_.get(),extent_,destination,destinationSize,vertex,display,sampler,nullptr);
    }
    void Clear(ID3D11DeviceContext* context) {
        if (target_) { float black[4]={0,0,0,1}; context->ClearRenderTargetView(target_.get(),black); }
    }
    Extent Size() const { return extent_; }
    ID3D11Texture2D* Texture() const { return texture_.get(); }
private:
    static void Draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* source,
        Extent sourceSize, ID3D11RenderTargetView* target, Extent targetSize,
        ID3D11VertexShader* vertex, ID3D11PixelShader* pixel,
        ID3D11SamplerState* sampler, ID3D11Buffer* settings) {
        float black[4]={0,0,0,1}; context->ClearRenderTargetView(target,black);
        auto fit=Fit(sourceSize.width,sourceSize.height,targetSize);
        D3D11_VIEWPORT viewport{fit.x,fit.y,fit.width,fit.height,0,1};
        context->RSSetViewports(1,&viewport);
        context->OMSetRenderTargets(1,&target,nullptr);
        context->PSSetConstantBuffers(0,1,&settings);
        context->PSSetShaderResources(0,1,&source);
        context->PSSetSamplers(0,1,&sampler);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertex,nullptr,0);
        context->PSSetShader(pixel,nullptr,0);
        context->Draw(3,0);
        ID3D11ShaderResourceView* none=nullptr;
        context->PSSetShaderResources(0,1,&none);
        context->OMSetRenderTargets(0,nullptr,nullptr);
    }
    Extent extent_{};
    DXGI_FORMAT format_=DXGI_FORMAT_UNKNOWN;
    winrt::com_ptr<ID3D11Texture2D> texture_;
    winrt::com_ptr<ID3D11RenderTargetView> target_;
    winrt::com_ptr<ID3D11ShaderResourceView> view_;
};
}
