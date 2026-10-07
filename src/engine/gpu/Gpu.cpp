#include "Gpu.h"

#include "Log.h"

#include <array>

namespace smoothcam
{
namespace
{
HRESULT CreateOnAdapter(IDXGIAdapter1* adapter, D3D_DRIVER_TYPE type, ComPtr<ID3D11Device>& device,
                        ComPtr<ID3D11DeviceContext>& context)
{
    static constexpr D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    UINT flags = D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : type, nullptr, flags, levels,
                                   ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(hr))
    {
        // Some adapters (and WARP on older builds) refuse VIDEO_SUPPORT; we can
        // live without it, MF then decodes on the CPU.
        flags &= ~D3D11_CREATE_DEVICE_VIDEO_SUPPORT;
        hr = D3D11CreateDevice(adapter, adapter ? D3D_DRIVER_TYPE_UNKNOWN : type, nullptr, flags, levels,
                               ARRAYSIZE(levels), D3D11_SDK_VERSION, &device, nullptr, &context);
    }
    return hr;
}
} // namespace

HRESULT GpuDevice::Create(std::unique_ptr<GpuDevice>& out, bool forceWarp)
{
    auto gpu = std::make_unique<GpuDevice>();

    ComPtr<IDXGIFactory6> factory;
    RETURN_IF_FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory)));

    HRESULT hr = E_FAIL;
    if (!forceWarp)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (SUCCEEDED(factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                          IID_PPV_ARGS(&adapter))))
        {
            hr = CreateOnAdapter(adapter.get(), D3D_DRIVER_TYPE_UNKNOWN, gpu->m_device, gpu->m_context);
            if (SUCCEEDED(hr))
                gpu->m_adapter = adapter;
        }
        if (FAILED(hr))
            hr = CreateOnAdapter(nullptr, D3D_DRIVER_TYPE_HARDWARE, gpu->m_device, gpu->m_context);
    }
    if (FAILED(hr))
    {
        SC_LOG_INFO(L"GPU: hardware device unavailable (0x%08X), using WARP", hr);
        RETURN_IF_FAILED(CreateOnAdapter(nullptr, D3D_DRIVER_TYPE_WARP, gpu->m_device, gpu->m_context));
    }

    if (!gpu->m_adapter)
    {
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        RETURN_IF_FAILED(gpu->m_device->QueryInterface(IID_PPV_ARGS(&dxgiDevice)));
        RETURN_IF_FAILED(dxgiDevice->GetAdapter(&adapter));
        RETURN_IF_FAILED(adapter->QueryInterface(IID_PPV_ARGS(&gpu->m_adapter)));
    }

    DXGI_ADAPTER_DESC1 desc{};
    RETURN_IF_FAILED(gpu->m_adapter->GetDesc1(&desc));
    gpu->m_info.name = desc.Description;
    gpu->m_info.vendorId = desc.VendorId;
    gpu->m_info.deviceId = desc.DeviceId;
    gpu->m_info.luid = desc.AdapterLuid;
    gpu->m_info.software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ||
                           (desc.VendorId == 0x1414 && desc.DeviceId == 0x8c);

    // Media Foundation uses the device from its own worker threads.
    if (SUCCEEDED(gpu->m_device->QueryInterface(IID_PPV_ARGS(&gpu->m_multithread))))
        gpu->m_multithread->SetMultithreadProtected(TRUE);

    RETURN_IF_FAILED(MFCreateDXGIDeviceManager(&gpu->m_resetToken, &gpu->m_manager));
    RETURN_IF_FAILED(gpu->m_manager->ResetDevice(gpu->m_device.get(), gpu->m_resetToken));

    SC_LOG_INFO(L"GPU: %s (vendor 0x%04X, device 0x%04X%s)", gpu->m_info.name.c_str(), gpu->m_info.vendorId,
                gpu->m_info.deviceId, gpu->m_info.software ? L", software" : L"");
    out = std::move(gpu);
    return S_OK;
}

HRESULT GpuDevice::CreateShader(const void* bytecode, size_t size, ComPtr<ID3D11ComputeShader>& out) const
{
    return m_device->CreateComputeShader(bytecode, size, nullptr, &out);
}

HRESULT Texture::Create(ID3D11Device* device, uint32_t w, uint32_t h, DXGI_FORMAT fmt, bool withUav)
{
    Reset();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = fmt;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | (withUav ? D3D11_BIND_UNORDERED_ACCESS : 0);
    RETURN_IF_FAILED(device->CreateTexture2D(&desc, nullptr, &tex));
    RETURN_IF_FAILED(device->CreateShaderResourceView(tex.get(), nullptr, &srv));
    if (withUav)
        RETURN_IF_FAILED(device->CreateUnorderedAccessView(tex.get(), nullptr, &uav));
    width = w;
    height = h;
    format = fmt;
    return S_OK;
}

HRESULT Texture::CreateStaging(ID3D11Device* device, uint32_t w, uint32_t h, DXGI_FORMAT fmt)
{
    Reset();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = fmt;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    RETURN_IF_FAILED(device->CreateTexture2D(&desc, nullptr, &tex));
    width = w;
    height = h;
    format = fmt;
    return S_OK;
}

HRESULT Nv12Texture::Create(ID3D11Device* device, uint32_t w, uint32_t h)
{
    *this = Nv12Texture{};
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    RETURN_IF_FAILED(device->CreateTexture2D(&desc, nullptr, &tex));

    D3D11_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    srv.Format = DXGI_FORMAT_R8_UNORM;
    RETURN_IF_FAILED(device->CreateShaderResourceView(tex.get(), &srv, &srvY));
    srv.Format = DXGI_FORMAT_R8G8_UNORM;
    RETURN_IF_FAILED(device->CreateShaderResourceView(tex.get(), &srv, &srvUV));
    width = w;
    height = h;
    return S_OK;
}

void RunCompute(ID3D11DeviceContext* context, ID3D11ComputeShader* shader, ID3D11Buffer* constants,
                std::initializer_list<ID3D11ShaderResourceView*> srvs,
                std::initializer_list<ID3D11UnorderedAccessView*> uavs, ID3D11SamplerState* sampler,
                uint32_t threadsX, uint32_t threadsY, uint32_t groupSize)
{
    std::array<ID3D11ShaderResourceView*, 8> srvArray{};
    std::array<ID3D11UnorderedAccessView*, 4> uavArray{};
    UINT srvCount = 0;
    UINT uavCount = 0;
    for (auto* s : srvs)
        srvArray[srvCount++] = s;
    for (auto* u : uavs)
        uavArray[uavCount++] = u;

    context->CSSetShader(shader, nullptr, 0);
    context->CSSetConstantBuffers(0, 1, &constants);
    if (sampler)
        context->CSSetSamplers(0, 1, &sampler);
    context->CSSetShaderResources(0, srvCount, srvArray.data());
    context->CSSetUnorderedAccessViews(0, uavCount, uavArray.data(), nullptr);

    context->Dispatch((threadsX + groupSize - 1) / groupSize, (threadsY + groupSize - 1) / groupSize, 1);

    std::array<ID3D11ShaderResourceView*, 8> nullSrvs{};
    std::array<ID3D11UnorderedAccessView*, 4> nullUavs{};
    context->CSSetShaderResources(0, srvCount, nullSrvs.data());
    context->CSSetUnorderedAccessViews(0, uavCount, nullUavs.data(), nullptr);
}

HRESULT CreateLinearClampSampler(ID3D11Device* device, ComPtr<ID3D11SamplerState>& out)
{
    D3D11_SAMPLER_DESC desc{};
    desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU = desc.AddressV = desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MaxLOD = D3D11_FLOAT32_MAX;
    return device->CreateSamplerState(&desc, &out);
}
} // namespace smoothcam
