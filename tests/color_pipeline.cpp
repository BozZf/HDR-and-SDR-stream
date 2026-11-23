// Execute the production HLSL on D3D11 WARP and read back float pixels.
// This tests the actual shader, not a second implementation of its formula.
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <winrt/base.h>
#include <array>
#include <vector>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include "shader_source.h"
using winrt::com_ptr;
using winrt::check_hresult;
using Pixel = std::array<float,4>;
void Require(bool condition, char const* message) {
    if (!condition) throw std::runtime_error(message);
}
com_ptr<ID3DBlob> Compile(char const* entry, char const* target) {
    com_ptr<ID3DBlob> code, errors;
    HRESULT result = D3DCompile(kColorShader,sizeof(kColorShader)-1,nullptr,nullptr,nullptr,
        entry,target,D3DCOMPILE_ENABLE_STRICTNESS,0,code.put(),errors.put());
    if (FAILED(result) && errors) std::cerr << static_cast<char*>(errors->GetBufferPointer());
    check_hresult(result); return code;
}
int main() {
    try {
        com_ptr<ID3D11Device> device;
        com_ptr<ID3D11DeviceContext> context;
        check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,device.put(),nullptr,context.put()));
        auto vcode=Compile("VS","vs_5_0"), pcode=Compile("PS","ps_5_0");
        com_ptr<ID3D11VertexShader> vs;
        com_ptr<ID3D11PixelShader> ps;
        check_hresult(device->CreateVertexShader(vcode->GetBufferPointer(),vcode->GetBufferSize(),nullptr,vs.put()));
        check_hresult(device->CreatePixelShader(pcode->GetBufferPointer(),pcode->GetBufferSize(),nullptr,ps.put()));
        std::vector<Pixel> input;
        // Neutral ramp through shadows, reference white, and HDR highlights.
        for (int i=0;i<=256;++i) { float x=i/32.0f; input.push_back({x,x,x,1}); }
        input.push_back({12,0,0,1}); input.push_back({0,12,0,1});
        input.push_back({0,0,12,1}); input.push_back({-0.1f,2,4,1});
        UINT width=static_cast<UINT>(input.size());
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width=width; desc.Height=1; desc.MipLevels=desc.ArraySize=1;
        desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; desc.SampleDesc.Count=1;
        desc.Usage=D3D11_USAGE_DEFAULT; desc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA data{input.data(),width*static_cast<UINT>(sizeof(Pixel)),0};
        com_ptr<ID3D11Texture2D> source, output, staging;
        check_hresult(device->CreateTexture2D(&desc,&data,source.put()));
        desc.BindFlags=D3D11_BIND_RENDER_TARGET;
        check_hresult(device->CreateTexture2D(&desc,nullptr,output.put()));
        desc.BindFlags=0; desc.Usage=D3D11_USAGE_STAGING; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
        check_hresult(device->CreateTexture2D(&desc,nullptr,staging.put()));
        com_ptr<ID3D11ShaderResourceView> image;
        com_ptr<ID3D11RenderTargetView> target;
        check_hresult(device->CreateShaderResourceView(source.get(),nullptr,image.put()));
        check_hresult(device->CreateRenderTargetView(output.get(),nullptr,target.put()));
        D3D11_SAMPLER_DESC sd{};
        sd.Filter=D3D11_FILTER_MIN_MAG_MIP_POINT;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD=D3D11_FLOAT32_MAX;
        com_ptr<ID3D11SamplerState> sampler;
        check_hresult(device->CreateSamplerState(&sd,sampler.put()));
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth=16; bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        com_ptr<ID3D11Buffer> settings;
        check_hresult(device->CreateBuffer(&bd,nullptr,settings.put()));
        D3D11_VIEWPORT viewport{0,0,static_cast<float>(width),1,0,1};
        context->RSSetViewports(1,&viewport);
        auto rtv=target.get(); context->OMSetRenderTargets(1,&rtv,nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vs.get(),nullptr,0); context->PSSetShader(ps.get(),nullptr,0);
        auto srv=image.get(); context->PSSetShaderResources(0,1,&srv);
        auto smp=sampler.get(); context->PSSetSamplers(0,1,&smp);
        auto cb=settings.get(); context->PSSetConstantBuffers(0,1,&cb);
        auto render = [&](float ev, float white) {
            float values[4]={ev,white,0.65f,0.0f};
            context->UpdateSubresource(settings.get(),0,nullptr,values,0,0);
            context->Draw(3,0);
            context->CopyResource(staging.get(),output.get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            check_hresult(context->Map(staging.get(),0,D3D11_MAP_READ,0,&mapped));
            std::vector<Pixel> result(width);
            std::memcpy(result.data(),mapped.pData,width*sizeof(Pixel));
            context->Unmap(staging.get(),0);
            return result;
        };
        auto sdr=render(0,203);
        for (auto const& p : sdr) for (float channel : p)
            Require(std::isfinite(channel) && channel>=-1e-5f && channel<=1.00001f,"SDR must be finite and bounded");
        Require(std::abs(sdr[0][0])<1e-6f,"Black must remain black");
        for (int i=1;i<=256;++i) {
            Require(sdr[i][0]>=sdr[i-1][0]-1e-5f,"Neutral ramp must be monotonic");
            Require(std::abs(sdr[i][0]-sdr[i][1])<1e-5f && std::abs(sdr[i][1]-sdr[i][2])<1e-5f,"Neutral must remain neutral");
        }
        Require(sdr[128][0]>sdr[64][0]+0.01f,"HDR highlights must retain separation");
        auto bright=render(1,203), dim=render(-1,203), highWhite=render(0,400);
        Require(bright[16][0]>sdr[16][0] && dim[16][0]<sdr[16][0],"Exposure control must work");
        Require(highWhite[16][0]<sdr[16][0],"Reference white must scale SDR brightness");
        std::cout << "PASS: HDR-to-SDR production HLSL, SDR range, black, monotonicity, neutrals, highlights, controls\n";
        return 0;
    } catch (winrt::hresult_error const& e) {
        std::wcerr << e.message().c_str() << L'\n'; return 1;
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}
