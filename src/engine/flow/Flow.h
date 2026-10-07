#pragma once

#include "gpu/Gpu.h"

#include <array>

namespace smoothcam
{
// Level 0 is half the source resolution, level 4 is 1/32.
inline constexpr int kPyramidLevels = 5;

struct LumaPyramid
{
    std::array<Texture, kPyramidLevels> level;
    uint32_t sourceWidth = 0;
    uint32_t sourceHeight = 0;

    HRESULT Ensure(ID3D11Device* device, uint32_t width, uint32_t height);
};

class PyramidBuilder
{
public:
    HRESULT Init(const GpuDevice& gpu);
    void Build(ID3D11DeviceContext* context, ID3D11ShaderResourceView* fullLuma, LumaPyramid& pyramid);

private:
    struct Params
    {
        uint32_t dstW, dstH, srcW, srcH;
    };
    ComPtr<ID3D11ComputeShader> m_shader;
    ConstantBuffer<Params> m_cb;
};

// Raw motion vectors for a pair of frames, in full-resolution source pixels,
// one vector per grid cell.
struct RawFlow
{
    const Texture* forward = nullptr;  // A -> B, on A's grid (RG16F)
    const Texture* backward = nullptr; // B -> A, on B's grid (RG16F)
};

class FlowEstimator
{
public:
    virtual ~FlowEstimator() = default;
    virtual const wchar_t* Name() const = 0;

    // `previousForward` is the refined forward field of the previous pair (or
    // null); estimators may use it as a temporal hint.
    virtual HRESULT Estimate(const LumaPyramid& a, const LumaPyramid& b, const Texture* previousForward,
                             RawFlow& out) = 0;

    // Pyramid level whose luma best matches the estimator's working resolution.
    virtual int WorkLevel() const = 0;
};

// Our own estimator: coarse-to-fine block matching in compute shaders. Runs on
// any D3D11 GPU, including WARP.
class ComputeFlow final : public FlowEstimator
{
public:
    // workLevel 0 = half resolution (Quality), 1 = quarter (Balanced).
    ComputeFlow(const GpuDevice& gpu, int workLevel) : m_gpu(gpu), m_start(workLevel) {}

    HRESULT Init();
    const wchar_t* Name() const override { return L"GPU compute"; }
    HRESULT Estimate(const LumaPyramid& a, const LumaPyramid& b, const Texture* previousForward,
                     RawFlow& out) override;
    int WorkLevel() const override { return m_start; }

private:
    struct MatchParams
    {
        uint32_t levelW, levelH;
        uint32_t blocksW, blocksH;
        uint32_t coarseW, coarseH;
        float temporalScale;
        uint32_t flags;
        int32_t searchRadius;
        float searchStep;
        float lambda;
        int32_t fineRadius;
        uint32_t blockStride;
        uint32_t coarseStride;
        float pad[2];
    };
    struct MedianParams
    {
        uint32_t w, h;
        float scale;
        float pad;
    };

    HRESULT EnsureFields(const LumaPyramid& pyramid);
    void RunDirection(const LumaPyramid& from, const LumaPyramid& to, const Texture* temporal, float temporalSign,
                      Texture& out);

    const GpuDevice& m_gpu;
    int m_start;
    int m_coarsest = 0;
    ComPtr<ID3D11ComputeShader> m_match;
    ComPtr<ID3D11ComputeShader> m_median;
    ComPtr<ID3D11SamplerState> m_sampler;
    ConstantBuffer<MatchParams> m_matchCb;
    ConstantBuffer<MedianParams> m_medianCb;
    std::array<Texture, kPyramidLevels> m_levelField;
    Texture m_forward;
    Texture m_backward;
    uint32_t m_sourceW = 0;
    uint32_t m_sourceH = 0;
};

// Turns raw vectors into refined fields (xy = vector, z = confidence).
class FlowRefiner
{
public:
    HRESULT Init(const GpuDevice& gpu);
    void Run(ID3D11DeviceContext* context, const Texture& field, const Texture& opposite, const Texture& lumaA,
             const Texture& lumaB, uint32_t sourceW, uint32_t sourceH, Texture& out);

private:
    struct Params
    {
        uint32_t gridW, gridH;
        float frameW, frameH;
        float cellW, cellH;
        float sigmaBase;
        float sigmaRel;
        float madScale;
        float pad[3];
    };
    ComPtr<ID3D11ComputeShader> m_shader;
    ComPtr<ID3D11SamplerState> m_sampler;
    ConstantBuffer<Params> m_cb;
};
} // namespace smoothcam
