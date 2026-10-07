#include "Flow.h"

#include "BlockMatch.h"
#include "Consistency.h"
#include "Downsample.h"
#include "VectorMedian.h"

#include <algorithm>

namespace smoothcam
{
namespace
{
uint32_t DivUp(uint32_t a, uint32_t b)
{
    return (a + b - 1) / b;
}

// Smallest pyramid level we still trust for block matching.
constexpr uint32_t kMinLevelSize = 24;

constexpr uint32_t kCoarseStride = 8;
constexpr uint32_t kFineStride = 4;
} // namespace

HRESULT LumaPyramid::Ensure(ID3D11Device* device, uint32_t width, uint32_t height)
{
    if (sourceWidth == width && sourceHeight == height && level[0])
        return S_OK;
    uint32_t w = width;
    uint32_t h = height;
    for (auto& tex : level)
    {
        w = std::max(1u, DivUp(w, 2));
        h = std::max(1u, DivUp(h, 2));
        RETURN_IF_FAILED(tex.Create(device, w, h, DXGI_FORMAT_R8_UNORM));
    }
    sourceWidth = width;
    sourceHeight = height;
    return S_OK;
}

HRESULT PyramidBuilder::Init(const GpuDevice& gpu)
{
    RETURN_IF_FAILED(gpu.CreateShader(g_Downsample, sizeof(g_Downsample), m_shader));
    return m_cb.Create(gpu.Device());
}

void PyramidBuilder::Build(ID3D11DeviceContext* context, ID3D11ShaderResourceView* fullLuma, LumaPyramid& pyramid)
{
    ID3D11ShaderResourceView* src = fullLuma;
    uint32_t srcW = pyramid.sourceWidth;
    uint32_t srcH = pyramid.sourceHeight;
    for (auto& level : pyramid.level)
    {
        m_cb.Update(context, {level.width, level.height, srcW, srcH});
        RunCompute(context, m_shader.get(), m_cb.Get(), {src}, {level.uav.get()}, nullptr, level.width,
                   level.height);
        src = level.srv.get();
        srcW = level.width;
        srcH = level.height;
    }
}

HRESULT ComputeFlow::Init()
{
    RETURN_IF_FAILED(m_gpu.CreateShader(g_BlockMatch, sizeof(g_BlockMatch), m_match));
    RETURN_IF_FAILED(m_gpu.CreateShader(g_VectorMedian, sizeof(g_VectorMedian), m_median));
    RETURN_IF_FAILED(CreateLinearClampSampler(m_gpu.Device(), m_sampler));
    RETURN_IF_FAILED(m_matchCb.Create(m_gpu.Device()));
    return m_medianCb.Create(m_gpu.Device());
}

HRESULT ComputeFlow::EnsureFields(const LumaPyramid& pyramid)
{
    if (m_sourceW == pyramid.sourceWidth && m_sourceH == pyramid.sourceHeight && m_forward)
        return S_OK;

    m_start = std::clamp(m_start, 0, kPyramidLevels - 1);
    m_coarsest = std::min(m_start + 3, kPyramidLevels - 1);
    while (m_coarsest > m_start && (pyramid.level[m_coarsest].width < kMinLevelSize ||
                                    pyramid.level[m_coarsest].height < kMinLevelSize))
        --m_coarsest;

    for (int l = m_start; l <= m_coarsest; ++l)
    {
        const uint32_t stride = l == m_start ? kFineStride : kCoarseStride;
        const auto& lv = pyramid.level[l];
        RETURN_IF_FAILED(m_levelField[l].Create(m_gpu.Device(), DivUp(lv.width, stride), DivUp(lv.height, stride),
                                                DXGI_FORMAT_R16G16_FLOAT));
    }
    const auto& fine = m_levelField[m_start];
    RETURN_IF_FAILED(m_forward.Create(m_gpu.Device(), fine.width, fine.height, DXGI_FORMAT_R16G16_FLOAT));
    RETURN_IF_FAILED(m_backward.Create(m_gpu.Device(), fine.width, fine.height, DXGI_FORMAT_R16G16_FLOAT));
    m_sourceW = pyramid.sourceWidth;
    m_sourceH = pyramid.sourceHeight;
    return S_OK;
}

void ComputeFlow::RunDirection(const LumaPyramid& from, const LumaPyramid& to, const Texture* temporal,
                               float temporalSign, Texture& out)
{
    auto* context = m_gpu.Context();
    for (int l = m_coarsest; l >= m_start; --l)
    {
        const bool finest = l == m_start;
        const bool coarsest = l == m_coarsest;
        const auto& lv = from.level[l];
        const auto& field = m_levelField[l];
        const auto* coarse = coarsest ? nullptr : &m_levelField[l + 1];

        MatchParams p{};
        p.levelW = lv.width;
        p.levelH = lv.height;
        p.blocksW = field.width;
        p.blocksH = field.height;
        p.coarseW = coarse ? coarse->width : 1;
        p.coarseH = coarse ? coarse->height : 1;
        p.temporalScale = temporalSign / static_cast<float>(1u << (l + 1));
        p.flags = (coarse ? 1u : 0u) | (temporal ? 2u : 0u);
        p.searchRadius = coarsest ? 4 : 1;
        p.searchStep = 1.0f;
        p.lambda = 0.003f;
        p.fineRadius = finest ? 1 : 0;
        p.blockStride = finest ? kFineStride : kCoarseStride;
        p.coarseStride = kCoarseStride;
        m_matchCb.Update(context, p);

        RunCompute(context, m_match.get(), m_matchCb.Get(),
                   {lv.srv.get(), to.level[l].srv.get(), coarse ? coarse->srv.get() : nullptr,
                    temporal ? temporal->srv.get() : nullptr},
                   {field.uav.get()}, m_sampler.get(), field.width, field.height);
    }

    const auto& fine = m_levelField[m_start];
    m_medianCb.Update(context, {fine.width, fine.height, static_cast<float>(1u << (m_start + 1)), 0});
    RunCompute(context, m_median.get(), m_medianCb.Get(), {fine.srv.get()}, {out.uav.get()}, nullptr, fine.width,
               fine.height);
}

HRESULT ComputeFlow::Estimate(const LumaPyramid& a, const LumaPyramid& b, const Texture* previousForward,
                              RawFlow& out)
{
    RETURN_IF_FAILED(EnsureFields(a));
    // The previous pair's forward motion is a good guess for this pair's
    // forward motion, and (negated) for its backward motion.
    RunDirection(a, b, previousForward, 1.0f, m_forward);
    RunDirection(b, a, previousForward, -1.0f, m_backward);
    out.forward = &m_forward;
    out.backward = &m_backward;
    return S_OK;
}

HRESULT FlowRefiner::Init(const GpuDevice& gpu)
{
    RETURN_IF_FAILED(gpu.CreateShader(g_Consistency, sizeof(g_Consistency), m_shader));
    RETURN_IF_FAILED(CreateLinearClampSampler(gpu.Device(), m_sampler));
    return m_cb.Create(gpu.Device());
}

void FlowRefiner::Run(ID3D11DeviceContext* context, const Texture& field, const Texture& opposite,
                      const Texture& lumaA, const Texture& lumaB, uint32_t sourceW, uint32_t sourceH, Texture& out)
{
    Params p{};
    p.gridW = field.width;
    p.gridH = field.height;
    p.frameW = static_cast<float>(sourceW);
    p.frameH = static_cast<float>(sourceH);
    p.cellW = p.frameW / field.width;
    p.cellH = p.frameH / field.height;
    p.sigmaBase = 2.0f;
    p.sigmaRel = 0.1f;
    p.madScale = 0.07f;
    m_cb.Update(context, p);
    RunCompute(context, m_shader.get(), m_cb.Get(),
               {field.srv.get(), opposite.srv.get(), lumaA.srv.get(), lumaB.srv.get()}, {out.uav.get()},
               m_sampler.get(), field.width, field.height);
}
} // namespace smoothcam
