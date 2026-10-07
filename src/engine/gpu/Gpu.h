#pragma once

#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <mfapi.h>

#include <wil/com.h>
#include <wil/result.h>

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>

namespace smoothcam
{
template <class T>
using ComPtr = wil::com_ptr_nothrow<T>;

struct GpuInfo
{
    std::wstring name;
    uint32_t vendorId = 0;
    uint32_t deviceId = 0;
    LUID luid{};
    bool software = false; // WARP / Microsoft Basic Render Driver

    bool IsNvidia() const { return vendorId == 0x10DE; }
};

class GpuDevice
{
public:
    // Creates a D3D11 device on the high-performance adapter (discrete GPU on
    // hybrid laptops), falling back to the default adapter and then WARP.
    static HRESULT Create(std::unique_ptr<GpuDevice>& out, bool forceWarp = false);

    ID3D11Device* Device() const { return m_device.get(); }
    ID3D11DeviceContext* Context() const { return m_context.get(); }
    IMFDXGIDeviceManager* Manager() const { return m_manager.get(); }
    IDXGIAdapter1* Adapter() const { return m_adapter.get(); }
    const GpuInfo& Info() const { return m_info; }

    HRESULT CreateShader(const void* bytecode, size_t size, ComPtr<ID3D11ComputeShader>& out) const;

    // Media Foundation shares this device from its own threads. Hold this
    // around a sequence of context calls that must not be interleaved.
    class ScopedLock
    {
    public:
        explicit ScopedLock(const GpuDevice& gpu) : m_mt(gpu.m_multithread.get())
        {
            if (m_mt)
                m_mt->Enter();
        }
        ~ScopedLock()
        {
            if (m_mt)
                m_mt->Leave();
        }
        ScopedLock(const ScopedLock&) = delete;
        ScopedLock& operator=(const ScopedLock&) = delete;

    private:
        ID3D11Multithread* m_mt;
    };

private:
    ComPtr<ID3D11Multithread> m_multithread;
    ComPtr<IDXGIAdapter1> m_adapter;
    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IMFDXGIDeviceManager> m_manager;
    UINT m_resetToken = 0;
    GpuInfo m_info;
};

// A 2D texture plus whichever views it was created with.
struct Texture
{
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11UnorderedAccessView> uav;
    uint32_t width = 0;
    uint32_t height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

    explicit operator bool() const { return tex != nullptr; }
    void Reset() { *this = Texture{}; }

    // Shader-readable and writable texture (most intermediate buffers).
    HRESULT Create(ID3D11Device* device, uint32_t w, uint32_t h, DXGI_FORMAT fmt, bool uav = true);
    HRESULT CreateStaging(ID3D11Device* device, uint32_t w, uint32_t h, DXGI_FORMAT fmt);
};

// Read-only view of a frame as two planes: luma (R8) and half-size chroma (R8G8).
struct FrameView
{
    ID3D11ShaderResourceView* srvY = nullptr;
    ID3D11ShaderResourceView* srvUV = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
};

// NV12 frame with separate views for the luma (R8) and chroma (R8G8) planes.
struct Nv12Texture
{
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11ShaderResourceView> srvY;
    ComPtr<ID3D11ShaderResourceView> srvUV;
    uint32_t width = 0;
    uint32_t height = 0;

    explicit operator bool() const { return tex != nullptr; }
    HRESULT Create(ID3D11Device* device, uint32_t w, uint32_t h);
    FrameView View() const { return {srvY.get(), srvUV.get(), width, height}; }
};

// The same two planes as separate writable textures (output of GPU passes).
struct PlanarFrame
{
    Texture y;
    Texture uv;

    explicit operator bool() const { return static_cast<bool>(y); }
    HRESULT Create(ID3D11Device* device, uint32_t w, uint32_t h)
    {
        if (y.width == w && y.height == h && y)
            return S_OK;
        RETURN_IF_FAILED(y.Create(device, w, h, DXGI_FORMAT_R8_UNORM));
        return uv.Create(device, (w + 1) / 2, (h + 1) / 2, DXGI_FORMAT_R8G8_UNORM);
    }
    FrameView View() const { return {y.srv.get(), uv.srv.get(), y.width, y.height}; }
};

// Constant buffer for a POD parameter struct.
template <class T>
class ConstantBuffer
{
public:
    HRESULT Create(ID3D11Device* device)
    {
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = (sizeof(T) + 15) & ~15u;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        return device->CreateBuffer(&desc, nullptr, &m_buffer);
    }
    void Update(ID3D11DeviceContext* context, const T& value)
    {
        alignas(16) uint8_t data[(sizeof(T) + 15) & ~15u]{};
        memcpy(data, &value, sizeof(T));
        context->UpdateSubresource(m_buffer.get(), 0, nullptr, data, 0, 0);
    }
    ID3D11Buffer* Get() const { return m_buffer.get(); }

private:
    ComPtr<ID3D11Buffer> m_buffer;
};

// Binds everything for one compute dispatch and unbinds afterwards so the
// next pass can read what this one wrote without D3D11 hazard warnings.
void RunCompute(ID3D11DeviceContext* context, ID3D11ComputeShader* shader, ID3D11Buffer* constants,
                std::initializer_list<ID3D11ShaderResourceView*> srvs,
                std::initializer_list<ID3D11UnorderedAccessView*> uavs, ID3D11SamplerState* sampler,
                uint32_t threadsX, uint32_t threadsY, uint32_t groupSize = 8);

HRESULT CreateLinearClampSampler(ID3D11Device* device, ComPtr<ID3D11SamplerState>& out);
} // namespace smoothcam
