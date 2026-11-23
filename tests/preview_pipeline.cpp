#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <winrt/base.h>
#include <array>
#include <cstdint>
#include <vector>
#include <cstring>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include "video_frame.h"
#include "sdr_readback.h"
#include "shader_source.h"
using winrt::com_ptr;
using winrt::check_hresult;
void Require(bool value,char const* detail) { if (!value) throw std::runtime_error(detail); }
com_ptr<ID3DBlob> Compile(char const* entry,char const* target) {
    com_ptr<ID3DBlob> code,errors;
    auto result=D3DCompile(kColorShader,sizeof(kColorShader)-1,nullptr,nullptr,nullptr,
        entry,target,D3DCOMPILE_ENABLE_STRICTNESS,0,code.put(),errors.put());
    if (FAILED(result) && errors) std::cerr<<static_cast<char*>(errors->GetBufferPointer());
    check_hresult(result); return code;
}
std::vector<unsigned char> Snapshot(ID3D11Device* device,ID3D11DeviceContext* context,
    ID3D11Texture2D* texture,unsigned bytesPerPixel) {
    D3D11_TEXTURE2D_DESC desc{}; texture->GetDesc(&desc);
    desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0; desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    com_ptr<ID3D11Texture2D> staging;
    check_hresult(device->CreateTexture2D(&desc,nullptr,staging.put()));
    context->CopyResource(staging.get(),texture);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check_hresult(context->Map(staging.get(),0,D3D11_MAP_READ,0,&mapped));
    size_t stride=static_cast<size_t>(desc.Width)*bytesPerPixel;
    std::vector<unsigned char> bytes(stride*desc.Height);
    for (unsigned y=0;y<desc.Height;++y)
        std::memcpy(bytes.data()+y*stride,static_cast<unsigned char*>(mapped.pData)+y*mapped.RowPitch,stride);
    context->Unmap(staging.get(),0); return bytes;
}
int main() {
    try {
        com_ptr<ID3D11Device> device;
        com_ptr<ID3D11DeviceContext> context;
        check_hresult(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_WARP,nullptr,0,nullptr,0,
            D3D11_SDK_VERSION,device.put(),nullptr,context.put()));
        auto vc=Compile("VS","vs_5_0"),pc=Compile("PS","ps_5_0"),dc=Compile("DisplayPS","ps_5_0");
        com_ptr<ID3D11VertexShader> vertex;
        com_ptr<ID3D11PixelShader> color,display;
        check_hresult(device->CreateVertexShader(vc->GetBufferPointer(),vc->GetBufferSize(),nullptr,vertex.put()));
        check_hresult(device->CreatePixelShader(pc->GetBufferPointer(),pc->GetBufferSize(),nullptr,color.put()));
        check_hresult(device->CreatePixelShader(dc->GetBufferPointer(),dc->GetBufferSize(),nullptr,display.put()));
        // Captured scRGB channels in half precision, including a highlight above SDR white.
        std::array<std::uint16_t,4> pixel{0x4400,0x3800,0x3400,0x3c00};
        D3D11_TEXTURE2D_DESC inputDesc{};
        inputDesc.Width=inputDesc.Height=inputDesc.MipLevels=inputDesc.ArraySize=1;
        inputDesc.SampleDesc.Count=1; inputDesc.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
        inputDesc.Usage=D3D11_USAGE_DEFAULT; inputDesc.BindFlags=D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA inputData{pixel.data(),static_cast<UINT>(sizeof(pixel)),0};
        com_ptr<ID3D11Texture2D> input;
        com_ptr<ID3D11ShaderResourceView> source;
        check_hresult(device->CreateTexture2D(&inputDesc,&inputData,input.put()));
        check_hresult(device->CreateShaderResourceView(input.get(),nullptr,source.put()));
        D3D11_BUFFER_DESC bd{}; bd.ByteWidth=16; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
        com_ptr<ID3D11Buffer> settings;
        check_hresult(device->CreateBuffer(&bd,nullptr,settings.put()));
        D3D11_SAMPLER_DESC sd{};
        sd.Filter=D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU=sd.AddressV=sd.AddressW=D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD=D3D11_FLOAT32_MAX;
        com_ptr<ID3D11SamplerState> sampler;
        check_hresult(device->CreateSamplerState(&sd,sampler.put()));
        bridge::SdrReadback compatibility;
        bool rejected=false;
        try { compatibility.Update(device.get(),context.get(),input.get()); }
        catch(winrt::hresult_error const&) { rejected=true; }
        Require(rejected,"GDI must reject raw FP16 instead of interpreting it as SDR");
        for (auto resolution : {bridge::Resolution::P720,bridge::Resolution::P1080}) {
            auto extent=bridge::OutputExtent(resolution);
            bridge::VideoFrame frame;
            frame.Ensure(device.get(),extent);
            float values[4]={0,203,0.65f,0.0f};
            context->UpdateSubresource(settings.get(),0,nullptr,values,0,0);
            frame.Process(context.get(),source.get(),{1,1},vertex.get(),color.get(),sampler.get(),settings.get());
            D3D11_TEXTURE2D_DESC processed{};
            frame.Texture()->GetDesc(&processed);
            Require(processed.Format==DXGI_FORMAT_B8G8R8A8_UNORM,"Processed HDR input must always produce a BGRA8 SDR frame");
            auto before=Snapshot(device.get(),context.get(),frame.Texture(),4);
            Require(compatibility.Update(device.get(),context.get(),frame.Texture()),
                "First SDR frame must request a repaint");
            Require(compatibility.Width()==extent.width && compatibility.Height()==extent.height,
                "GDI readback must retain selected resolution");
            Require(compatibility.Pixels()==before,"GDI pixels must match the corrected SDR GPU frame");
            Require(!compatibility.Update(device.get(),context.get(),frame.Texture()),
                "An unchanged frame must not request another repaint");
            frame.Clear(context.get());
            Require(compatibility.Update(device.get(),context.get(),frame.Texture()),
                "Changed pixels at the same resolution must request a repaint");
            Require(compatibility.Pixels()!=before,"Readback must replace stale frame pixels");
            frame.Process(context.get(),source.get(),{1,1},vertex.get(),color.get(),sampler.get(),settings.get());
            compatibility.Clear();
            Require(compatibility.Pixels().empty(),"Clear compatibility pixels on stop");

            auto original=frame.Texture();
            for (auto mini : {bridge::Extent{320,180},bridge::Extent{480,270}}) {
                auto desc=inputDesc;
                desc.Width=mini.width; desc.Height=mini.height; desc.BindFlags=D3D11_BIND_RENDER_TARGET;
                desc.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
                com_ptr<ID3D11Texture2D> preview;
                com_ptr<ID3D11RenderTargetView> target;
                check_hresult(device->CreateTexture2D(&desc,nullptr,preview.put()));
                check_hresult(device->CreateRenderTargetView(preview.get(),nullptr,target.put()));
                frame.Blit(context.get(),target.get(),mini,vertex.get(),display.get(),sampler.get());
                auto shown=Snapshot(device.get(),context.get(),preview.get(),16);
                std::array<float,4> actual{};
                size_t center=static_cast<size_t>(mini.height/2)*mini.width+mini.width/2;
                std::memcpy(actual.data(),shown.data()+center*16,16);
                size_t offset=(static_cast<size_t>(extent.height/2)*extent.width+extent.width/2)*4;
                std::array<float,4> expected{before[offset+2]/255.f,before[offset+1]/255.f,before[offset]/255.f,1};
                for (unsigned c=0;c<4;++c)
                    Require(std::abs(actual[c]-expected[c])<1e-4f,"Preview must preserve processed colors without a second tone map");
                frame.Ensure(device.get(),extent);
                Require(frame.Texture()==original,"Preview size must not reallocate the program frame");
                D3D11_TEXTURE2D_DESC unchanged{}; frame.Texture()->GetDesc(&unchanged);
                Require(unchanged.Width==extent.width && unchanged.Height==extent.height,"Program must stay at 720p/1080p");
                Require(Snapshot(device.get(),context.get(),frame.Texture(),4)==before,
                    "Preview must not modify the full-resolution program frame");
            }
            frame.Clear(context.get());
            auto cleared=Snapshot(device.get(),context.get(),frame.Texture(),4);
            Require(cleared!=before,"Stopping must clear captured content");
        }
        std::cout<<"PASS: FP16 input, SDR output at 720p/1080p, pixel-exact GDI, unchanged preview colors and clear\n";
        return 0;
    } catch(winrt::hresult_error const& e) { std::wcerr<<e.message().c_str()<<L'\n'; return 1; }
      catch(std::exception const& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
