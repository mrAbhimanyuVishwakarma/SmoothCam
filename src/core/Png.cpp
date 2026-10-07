#include "Png.h"

#include <wincodec.h>

#include <wil/com.h>
#include <wil/result.h>

namespace smoothcam
{
HRESULT ReadImageBgra(const std::filesystem::path& path, std::vector<uint8_t>& pixels, uint32_t& width,
                      uint32_t& height)
{
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    RETURN_IF_FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));
    wil::com_ptr_nothrow<IWICBitmapDecoder> decoder;
    RETURN_IF_FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                        WICDecodeMetadataCacheOnDemand, &decoder));
    wil::com_ptr_nothrow<IWICBitmapFrameDecode> frame;
    RETURN_IF_FAILED(decoder->GetFrame(0, &frame));
    wil::com_ptr_nothrow<IWICFormatConverter> converter;
    RETURN_IF_FAILED(factory->CreateFormatConverter(&converter));
    RETURN_IF_FAILED(converter->Initialize(frame.get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone,
                                           nullptr, 0.0, WICBitmapPaletteTypeCustom));
    RETURN_IF_FAILED(converter->GetSize(&width, &height));
    pixels.resize(static_cast<size_t>(width) * height * 4);
    return converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data());
}

HRESULT WritePngBgra(const std::filesystem::path& path, const uint8_t* pixels, uint32_t width, uint32_t height,
                     uint32_t stride)
{
    wil::com_ptr_nothrow<IWICImagingFactory> factory;
    RETURN_IF_FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)));

    wil::com_ptr_nothrow<IWICStream> stream;
    RETURN_IF_FAILED(factory->CreateStream(&stream));
    RETURN_IF_FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE));

    wil::com_ptr_nothrow<IWICBitmapEncoder> encoder;
    RETURN_IF_FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder));
    RETURN_IF_FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache));

    wil::com_ptr_nothrow<IWICBitmapFrameEncode> frame;
    RETURN_IF_FAILED(encoder->CreateNewFrame(&frame, nullptr));
    RETURN_IF_FAILED(frame->Initialize(nullptr));
    RETURN_IF_FAILED(frame->SetSize(width, height));
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    RETURN_IF_FAILED(frame->SetPixelFormat(&format));
    RETURN_HR_IF(E_UNEXPECTED, format != GUID_WICPixelFormat32bppBGRA);
    RETURN_IF_FAILED(frame->WritePixels(height, stride, stride * height, const_cast<BYTE*>(pixels)));
    RETURN_IF_FAILED(frame->Commit());
    return encoder->Commit();
}
} // namespace smoothcam
