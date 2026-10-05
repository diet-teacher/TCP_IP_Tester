#pragma once
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <filesystem>
#include "protocol.h"
namespace imaging {
using Microsoft::WRL::ComPtr;
inline void checked(HRESULT result) {
    if (FAILED(result)) throw std::runtime_error("Windows image codec error: " + std::to_string(static_cast<unsigned long>(result)));
}
inline ComPtr<IWICImagingFactory> factory() {
    ComPtr<IWICImagingFactory> f;
    checked(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&f))); return f;
}
inline WICPixelFormatGUID format(uint32_t channels) {
    return channels == 1 ? GUID_WICPixelFormat8bppGray : channels == 3 ? GUID_WICPixelFormat24bppRGB : GUID_WICPixelFormat32bppRGBA;
}
inline wire::Bytes load(const std::filesystem::path& path) {
    auto f = factory(); ComPtr<IWICBitmapDecoder> decoder; ComPtr<IWICBitmapFrameDecode> frame;
    checked(f->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder));
    checked(decoder->GetFrame(0,&frame));
    UINT w,h; checked(frame->GetSize(&w,&h));
    WICPixelFormatGUID source; checked(frame->GetPixelFormat(&source));
    ComPtr<IWICComponentInfo> component; ComPtr<IWICPixelFormatInfo2> info;
    checked(f->CreateComponentInfo(source,&component)); checked(component.As(&info));
    UINT count; BOOL alpha; checked(info->GetChannelCount(&count)); checked(info->SupportsTransparency(&alpha));
    bool gray = IsEqualGUID(source,GUID_WICPixelFormatBlackWhite) || IsEqualGUID(source,GUID_WICPixelFormat2bppGray) ||
        IsEqualGUID(source,GUID_WICPixelFormat4bppGray) || IsEqualGUID(source,GUID_WICPixelFormat8bppGray);
    UINT bits; checked(info->GetBitsPerPixel(&bits));
    // Explicitly reject high-depth input instead of silently losing precision.
    if ((count && bits / count > 8) && !IsEqualGUID(source,GUID_WICPixelFormat32bppBGR))
        throw std::runtime_error("only 8-bit/channel images supported; convert high-depth input explicitly");
    if (IsEqualGUID(source,GUID_WICPixelFormat1bppIndexed) || IsEqualGUID(source,GUID_WICPixelFormat2bppIndexed) ||
        IsEqualGUID(source,GUID_WICPixelFormat4bppIndexed) || IsEqualGUID(source,GUID_WICPixelFormat8bppIndexed)) {
        ComPtr<IWICPalette> palette; checked(f->CreatePalette(&palette)); checked(frame->CopyPalette(palette.Get()));
        checked(palette->HasAlpha(&alpha)); gray = false;
    }
    uint32_t channels = gray ? 1 : alpha ? 4 : 3;
    uint64_t size = uint64_t(w)*h*channels;
    if (size > wire::MaxImageBytes) throw std::runtime_error("decoded image exceeds 64 MiB");
    wire::ImageInfo m{w,h,channels,8,w*channels,uint32_t(size)}; wire::validateImage(m);
    ComPtr<IWICFormatConverter> converter; checked(f->CreateFormatConverter(&converter));
    checked(converter->Initialize(frame.Get(),format(channels),WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    wire::Bytes pixels(m.size); checked(converter->CopyPixels(nullptr,m.stride,m.size,pixels.data()));
    return wire::imagePayload(m,pixels);
}
inline void save(const std::filesystem::path& path, const wire::Bytes& payload) {
    auto m = wire::imageInfo(payload); auto f = factory();
    ComPtr<IWICStream> stream; checked(f->CreateStream(&stream)); checked(stream->InitializeFromFilename(path.c_str(),GENERIC_WRITE));
    ComPtr<IWICBitmapEncoder> encoder; checked(f->CreateEncoder(GUID_ContainerFormatPng,nullptr,&encoder));
    checked(encoder->Initialize(stream.Get(),WICBitmapEncoderNoCache));
    ComPtr<IWICBitmapFrameEncode> frame; checked(encoder->CreateNewFrame(&frame,nullptr)); checked(frame->Initialize(nullptr));
    checked(frame->SetSize(m.width,m.height)); auto fmt = format(m.channels); checked(frame->SetPixelFormat(&fmt));
    ComPtr<IWICBitmap> bitmap;
    checked(f->CreateBitmapFromMemory(m.width,m.height,format(m.channels),m.stride,m.size,
        const_cast<BYTE*>(payload.data()+wire::ImageHeaderSize),&bitmap));
    ComPtr<IWICFormatConverter> converter; checked(f->CreateFormatConverter(&converter));
    checked(converter->Initialize(bitmap.Get(),fmt,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom));
    checked(frame->WriteSource(converter.Get(),nullptr));
    checked(frame->Commit()); checked(encoder->Commit());
}
// Preview only: nearest-neighbour downsampling; transmitted pixels remain untouched.
inline HBITMAP thumbnail(const wire::Bytes& payload) {
    auto m = wire::imageInfo(payload); double scale = (std::min)(250.0/m.width,140.0/m.height);
    int w = (std::max)(1,int(m.width*scale)), h = (std::max)(1,int(m.height*scale));
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = w;
    info.bmiHeader.biHeight = -h; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32; info.bmiHeader.biCompression = BI_RGB;
    void* data = nullptr; HBITMAP bitmap = CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&data,nullptr,0);
    if (!bitmap) throw std::runtime_error("preview allocation failed");
    auto* dst = static_cast<uint8_t*>(data); const auto* src = payload.data()+wire::ImageHeaderSize;
    for (int y=0;y<h;++y) for (int x=0;x<w;++x) {
        auto p = src + size_t(uint64_t(y)*m.height/h)*m.stride + size_t(uint64_t(x)*m.width/w)*m.channels;
        auto q = dst + (size_t(y)*w+x)*4;
        q[0] = m.channels == 1 ? p[0] : p[2]; q[1] = m.channels == 1 ? p[0] : p[1]; q[2] = p[0]; q[3] = 255;
    } return bitmap;
}
}
