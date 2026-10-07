#include "Compositor.h"

#include "BgComposite.h"
#include "BgDown.h"
#include "Blur.h"
#include "Composite.h"
#include "FsrConstants.h"
#include "FsrEasu.h"
#include "FsrRcas.h"
#include "LowLight.h"
#include "NisConstants.h"
#include "NisScaler.h"
#include "ScaleUV.h"
#include "ToBgra.h"

#include "Clock.h"

#include <algorithm>
#include <cstring>
#include <future>
#include <thread>

namespace smoothcam
{
HRESULT Compositor::Init(const GpuDevice& gpu)
{
    m_gpu = &gpu;
    RETURN_IF_FAILED(gpu.CreateShader(g_Composite, sizeof(g_Composite), m_composite));
    RETURN_IF_FAILED(gpu.CreateShader(g_ToBgra, sizeof(g_ToBgra), m_toBgra));
    RETURN_IF_FAILED(gpu.CreateShader(g_FsrEasu, sizeof(g_FsrEasu), m_easu));
    RETURN_IF_FAILED(gpu.CreateShader(g_FsrRcas, sizeof(g_FsrRcas), m_rcas));
    RETURN_IF_FAILED(gpu.CreateShader(g_ScaleUV, sizeof(g_ScaleUV), m_scaleUV));
    RETURN_IF_FAILED(gpu.CreateShader(g_LowLight, sizeof(g_LowLight), m_lowLight));
    RETURN_IF_FAILED(gpu.CreateShader(g_BgDown, sizeof(g_BgDown), m_bgDown));
    RETURN_IF_FAILED(gpu.CreateShader(g_Blur, sizeof(g_Blur), m_blur));
    RETURN_IF_FAILED(gpu.CreateShader(g_BgComposite, sizeof(g_BgComposite), m_bgComposite));
    RETURN_IF_FAILED(gpu.CreateShader(g_NisScaler, sizeof(g_NisScaler), m_nis));
    RETURN_IF_FAILED(CreateLinearClampSampler(gpu.Device(), m_sampler));
    auto* d = gpu.Device();

    // NIS filter tables: 64 phases x 8 taps, as 2x64 RGBA32F textures.
    for (auto [table, tex] : {std::pair{NisCoefScale(), &m_nisCoefScale}, std::pair{NisCoefUsm(), &m_nisCoefUsm}})
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = kNisCoefWidth;
        desc.Height = kNisCoefHeight;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_IMMUTABLE;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA init{table, kNisCoefWidth * 4 * sizeof(float), 0};
        RETURN_IF_FAILED(d->CreateTexture2D(&desc, &init, &tex->tex));
        RETURN_IF_FAILED(d->CreateShaderResourceView(tex->tex.get(), nullptr, &tex->srv));
    }
    RETURN_IF_FAILED(m_nisCb.Create(d));
    RETURN_IF_FAILED(m_compositeCb.Create(d));
    RETURN_IF_FAILED(m_bgraCb.Create(d));
    RETURN_IF_FAILED(m_easuCb.Create(d));
    RETURN_IF_FAILED(m_rcasCb.Create(d));
    RETURN_IF_FAILED(m_sizeCb.Create(d));
    RETURN_IF_FAILED(m_lowLightCb.Create(d));
    RETURN_IF_FAILED(m_bgDownCb.Create(d));
    RETURN_IF_FAILED(m_blurCb.Create(d));
    return m_bgCompositeCb.Create(d);
}

HRESULT Compositor::EnsureOutput(uint32_t width, uint32_t height, bool rgb32)
{
    if (width == m_width && height == m_height && rgb32 == m_rgb32 && m_out[0])
        return S_OK;
    auto* device = m_gpu->Device();
    const uint32_t cw = (width + 1) / 2;
    const uint32_t ch = (height + 1) / 2;
    RETURN_IF_FAILED(m_out[0].Create(device, width, height));
    RETURN_IF_FAILED(m_out[1].Create(device, width, height));
    RETURN_IF_FAILED(m_bgLow.Create(device, std::max(1u, width / 4), std::max(1u, height / 4),
                                    DXGI_FORMAT_R16G16B16A16_FLOAT));
    RETURN_IF_FAILED(m_bgTmp.Create(device, std::max(1u, width / 4), std::max(1u, height / 4),
                                    DXGI_FORMAT_R16G16B16A16_FLOAT));
    m_outBgra.Reset();
    m_stageY.Reset();
    m_stageUV.Reset();
    m_stageBgra.Reset();
    if (rgb32)
    {
        RETURN_IF_FAILED(m_outBgra.Create(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM));
        RETURN_IF_FAILED(m_stageBgra.CreateStaging(device, width, height, DXGI_FORMAT_R8G8B8A8_UNORM));
    }
    else
    {
        RETURN_IF_FAILED(m_stageY.CreateStaging(device, width, height, DXGI_FORMAT_R8_UNORM));
        RETURN_IF_FAILED(m_stageUV.CreateStaging(device, cw, ch, DXGI_FORMAT_R8G8_UNORM));
    }
    m_width = width;
    m_height = height;
    m_rgb32 = rgb32;
    m_finalY = &m_out[0].y;
    m_finalUV = &m_out[0].uv;
    return S_OK;
}

HRESULT Compositor::EnsureRender(uint32_t w, uint32_t h)
{
    RETURN_IF_FAILED(m_render[0].Create(m_gpu->Device(), w, h));
    return m_render[1].Create(m_gpu->Device(), w, h);
}

PlanarFrame& Compositor::OtherOutput(const Texture* inUse)
{
    return inUse == &m_out[0].y || inUse == &m_out[0].uv ? m_out[1] : m_out[0];
}

void Compositor::Compose(const FrameView& prev, const FrameView& next, const Texture* flowForward,
                         const Texture* flowBackward, const ComposeParams& params)
{
    auto* context = m_gpu->Context();

    // How many real camera pixels cover the output. If that's clearly fewer
    // than the output has, build the frame at camera resolution and upscale it
    // with FSR 1 instead of stretching it bilinearly.
    const float cropW = prev.width * params.cropScale[0];
    const float cropH = prev.height * params.cropScale[1];
    const bool upscale = m_width > cropW * 1.1f || m_height > cropH * 1.1f;
    if (upscale)
    {
        // Even sizes keep the NV12 chroma grid aligned.
        m_renderW = std::min(m_width, std::max(2u, static_cast<uint32_t>(cropW + 1.0f) & ~1u));
        m_renderH = std::min(m_height, std::max(2u, static_cast<uint32_t>(cropH + 1.0f) & ~1u));
    }
    else
    {
        m_renderW = m_width;
        m_renderH = m_height;
    }
    if (FAILED(EnsureRender(m_renderW, m_renderH)))
        return;

    // 1. Interpolate / pace into a render-size frame.
    CompositeCb cb{};
    cb.outW = m_renderW;
    cb.outH = m_renderH;
    cb.srcW = static_cast<float>(prev.width);
    cb.srcH = static_cast<float>(prev.height);
    cb.cropOffX = params.cropOffset[0];
    cb.cropOffY = params.cropOffset[1];
    cb.cropScaleX = params.cropScale[0];
    cb.cropScaleY = params.cropScale[1];
    cb.alpha = params.alpha;
    cb.mode = static_cast<uint32_t>(params.mode);
    cb.compare = params.compare ? 1u : 0u;
    cb.confGain = 1.6f;
    m_compositeCb.Update(context, cb);
    RunCompute(context, m_composite.get(), m_compositeCb.Get(),
               {prev.srvY, prev.srvUV, next.srvY, next.srvUV, flowForward ? flowForward->srv.get() : nullptr,
                flowBackward ? flowBackward->srv.get() : nullptr},
               {m_render[0].y.uav.get(), m_render[0].uv.uav.get()}, m_sampler.get(), (m_renderW + 1) / 2,
               (m_renderH + 1) / 2);
    const PlanarFrame* cur = &m_render[0];

    // 2. Low-light boost, at render size (cheaper than after upscaling).
    if (params.lowLightMaxGain > 1.0f && params.lumaStats)
    {
        m_lowLightCb.Update(context, {m_renderW, m_renderH, params.lowLightTarget, params.lowLightMaxGain});
        RunCompute(context, m_lowLight.get(), m_lowLightCb.Get(), {cur->y.srv.get(), cur->uv.srv.get(), params.lumaStats},
                   {m_render[1].y.uav.get(), m_render[1].uv.uav.get()}, nullptr, (m_renderW + 1) / 2,
                   (m_renderH + 1) / 2);
        cur = &m_render[1];
    }

    const Texture* curY = &cur->y;
    const Texture* curUV = &cur->uv;

    // 3. Upscale to the output size.
    m_usedNis = upscale && params.useNis && NisCanScale(m_renderW, m_renderH, m_width, m_height);
    if (upscale)
    {
        PlanarFrame& dst = m_out[0];
        if (m_usedNis)
        {
            // NIS's own sharpening, from our RCAS strength (stops, 0 = strongest).
            const float nisSharpness =
                params.sharpness < 0.0f ? 0.0f : std::clamp(0.6f - params.sharpness * 0.5f, 0.0f, 1.0f);
            if (m_nisKey.inW != m_renderW || m_nisKey.inH != m_renderH || m_nisKey.outW != m_width ||
                m_nisKey.outH != m_height || m_nisKey.sharpness != nisSharpness)
            {
                NisCb nis{};
                ComputeNisConstants(nis.data, nisSharpness, m_renderW, m_renderH, m_width, m_height);
                m_nisCb.Update(context, nis);
                m_nisKey = {m_renderW, m_renderH, m_width, m_height, nisSharpness};
            }
            // One thread group per 32x24 output block.
            RunCompute(context, m_nis.get(), m_nisCb.Get(),
                       {curY->srv.get(), m_nisCoefScale.srv.get(), m_nisCoefUsm.srv.get()}, {dst.y.uav.get()},
                       m_sampler.get(), (m_width + kNisBlockWidth - 1) / kNisBlockWidth,
                       (m_height + kNisBlockHeight - 1) / kNisBlockHeight, 1);
        }
        else
        {
            EasuCb easu{};
            ComputeEasuConstants(easu.con, static_cast<float>(m_renderW), static_cast<float>(m_renderH),
                                 static_cast<float>(m_width), static_cast<float>(m_height));
            easu.outW = m_width;
            easu.outH = m_height;
            m_easuCb.Update(context, easu);
            RunCompute(context, m_easu.get(), m_easuCb.Get(), {curY->srv.get()}, {dst.y.uav.get()}, m_sampler.get(),
                       m_width, m_height);
        }
        m_sizeCb.Update(context, {(m_width + 1) / 2, (m_height + 1) / 2, {}});
        RunCompute(context, m_scaleUV.get(), m_sizeCb.Get(), {curUV->srv.get()}, {dst.uv.uav.get()}, m_sampler.get(),
                   (m_width + 1) / 2, (m_height + 1) / 2);
        curY = &dst.y;
        curUV = &dst.uv;
    }

    // 4. Background blur, guided by the person mask.
    if (params.personMask)
    {
        const uint32_t lw = m_bgLow.width;
        const uint32_t lh = m_bgLow.height;
        m_bgDownCb.Update(context, {lw, lh, m_width, m_height});
        RunCompute(context, m_bgDown.get(), m_bgDownCb.Get(), {curY->srv.get(), curUV->srv.get()},
                   {m_bgLow.uav.get()}, nullptr, lw, lh);
        for (int pass = 0; pass < std::max(1, params.blurPasses); ++pass)
        {
            m_blurCb.Update(context, {lw, lh, 1, 0});
            RunCompute(context, m_blur.get(), m_blurCb.Get(), {m_bgLow.srv.get()}, {m_bgTmp.uav.get()}, nullptr, lw, lh);
            m_blurCb.Update(context, {lw, lh, 0, 1});
            RunCompute(context, m_blur.get(), m_blurCb.Get(), {m_bgTmp.srv.get()}, {m_bgLow.uav.get()}, nullptr, lw, lh);
        }
        PlanarFrame& dst = OtherOutput(curY);
        BgCompositeCb bc{};
        bc.w = m_width;
        bc.h = m_height;
        bc.cropOffX = params.cropOffset[0];
        bc.cropOffY = params.cropOffset[1];
        bc.cropScaleX = params.cropScale[0];
        bc.cropScaleY = params.cropScale[1];
        bc.edge0 = 0.35f;
        bc.edge1 = 0.65f;
        m_bgCompositeCb.Update(context, bc);
        RunCompute(context, m_bgComposite.get(), m_bgCompositeCb.Get(),
                   {curY->srv.get(), curUV->srv.get(), m_bgLow.srv.get(), params.personMask},
                   {dst.y.uav.get(), dst.uv.uav.get()}, m_sampler.get(), (m_width + 1) / 2, (m_height + 1) / 2);
        curY = &dst.y;
        curUV = &dst.uv;
    }

    // 5. Sharpen luma (NIS already did).
    if (params.sharpness >= 0.0f && !m_usedNis)
    {
        PlanarFrame& dst = OtherOutput(curY);
        RcasCb rcas{};
        ComputeRcasConstants(rcas.con, params.sharpness);
        rcas.w = m_width;
        rcas.h = m_height;
        m_rcasCb.Update(context, rcas);
        RunCompute(context, m_rcas.get(), m_rcasCb.Get(), {curY->srv.get()}, {dst.y.uav.get()}, nullptr, m_width,
                   m_height);
        curY = &dst.y;
    }

    m_finalY = curY;
    m_finalUV = curUV;

    if (m_rgb32)
    {
        // Webcams deliver BT.601 below 720p and BT.709 from 720p up.
        m_bgraCb.Update(context, {m_width, m_height, prev.height >= 720 ? 1u : 0u, 0});
        RunCompute(context, m_toBgra.get(), m_bgraCb.Get(), {m_finalY->srv.get(), m_finalUV->srv.get()},
                   {m_outBgra.uav.get()}, nullptr, m_width, m_height);
    }
}

namespace
{
// A 4K RGB32 frame is 33 MB; one core copies that in ~6-8 ms, which alone eats
// half of a 60 fps frame. Big planes are split across a few cores.
HRESULT CopyPlane(ID3D11DeviceContext* context, ID3D11Texture2D* staging, uint8_t* dst, LONG pitch,
                  uint32_t rowBytes, uint32_t rows, double& copyMs)
{
    D3D11_MAPPED_SUBRESOURCE mapped{};
    RETURN_IF_FAILED(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped));
    const Hns start = QpcNow();
    const auto* src = static_cast<const uint8_t*>(mapped.pData);
    const uint32_t copy = std::min<uint32_t>(rowBytes, static_cast<uint32_t>(std::abs(pitch)));
    auto copyRows = [&](uint32_t y0, uint32_t y1) {
        for (uint32_t y = y0; y < y1; ++y)
            memcpy(dst + static_cast<ptrdiff_t>(y) * pitch, src + static_cast<size_t>(y) * mapped.RowPitch, copy);
    };
    constexpr size_t kSplitAbove = 4 * 1024 * 1024;
    const uint32_t parts = static_cast<size_t>(copy) * rows > kSplitAbove
                               ? std::clamp(std::thread::hardware_concurrency() / 2, 1u, 4u)
                               : 1u;
    if (parts > 1)
    {
        std::future<void> helpers[3];
        const uint32_t step = (rows + parts - 1) / parts;
        for (uint32_t i = 1; i < parts; ++i)
            helpers[i - 1] = std::async(std::launch::async, copyRows, i * step, std::min(rows, (i + 1) * step));
        copyRows(0, std::min(rows, step));
        for (uint32_t i = 1; i < parts; ++i)
            helpers[i - 1].get();
    }
    else
    {
        copyRows(0, rows);
    }
    copyMs += HnsToMs(QpcNow() - start);
    context->Unmap(staging, 0);
    return S_OK;
}
} // namespace

HRESULT Compositor::Readback(uint8_t* dst, LONG pitch, DWORD capacity)
{
    auto* context = m_gpu->Context();
    const uint32_t cw = (m_width + 1) / 2;
    const uint32_t ch = (m_height + 1) / 2;
    m_lastCopyMs = 0;
    if (m_rgb32)
    {
        RETURN_HR_IF(E_INVALIDARG, static_cast<uint64_t>(std::abs(pitch)) * m_height > capacity);
        context->CopyResource(m_stageBgra.tex.get(), m_outBgra.tex.get());
        return CopyPlane(context, m_stageBgra.tex.get(), dst, pitch, m_width * 4, m_height, m_lastCopyMs);
    }

    RETURN_HR_IF(E_INVALIDARG, pitch <= 0 || static_cast<uint64_t>(pitch) * (m_height + ch) > capacity);
    context->CopyResource(m_stageY.tex.get(), m_finalY->tex.get());
    context->CopyResource(m_stageUV.tex.get(), m_finalUV->tex.get());
    RETURN_IF_FAILED(CopyPlane(context, m_stageY.tex.get(), dst, pitch, m_width, m_height, m_lastCopyMs));
    return CopyPlane(context, m_stageUV.tex.get(), dst + static_cast<ptrdiff_t>(pitch) * m_height, pitch, cw * 2, ch,
                     m_lastCopyMs);
}

void Compositor::FillBlank(uint8_t* dst, LONG pitch, DWORD capacity) const
{
    const uint32_t absPitch = static_cast<uint32_t>(std::abs(pitch));
    if (m_rgb32)
    {
        if (static_cast<uint64_t>(absPitch) * m_height > capacity)
            return;
        for (uint32_t y = 0; y < m_height; ++y)
        {
            auto* row = reinterpret_cast<uint32_t*>(dst + static_cast<ptrdiff_t>(y) * pitch);
            std::fill(row, row + m_width, 0xFF202020u);
        }
        return;
    }
    const uint32_t ch = (m_height + 1) / 2;
    if (pitch <= 0 || static_cast<uint64_t>(pitch) * (m_height + ch) > capacity)
        return;
    for (uint32_t y = 0; y < m_height; ++y)
        memset(dst + static_cast<size_t>(y) * pitch, 32, m_width);
    for (uint32_t y = 0; y < ch; ++y)
        memset(dst + static_cast<size_t>(m_height + y) * pitch, 128, ((m_width + 1) / 2) * 2);
}
} // namespace smoothcam
