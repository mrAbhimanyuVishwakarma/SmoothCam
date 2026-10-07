#pragma once

#include "gpu/Gpu.h"

namespace smoothcam
{
enum class ComposeMode : uint32_t
{
    Hold = 0,
    Blend = 1,
    Interpolate = 2,
    Extrapolate = 3,
};

struct ComposeParams
{
    ComposeMode mode = ComposeMode::Hold;
    float alpha = 0;
    bool compare = false;
    // Region of the source frame that becomes the output (aspect crop, and
    // auto-framing zoom), in normalized source coordinates.
    float cropOffset[2] = {0, 0};
    float cropScale[2] = {1, 1};
    // RCAS sharpening in stops (0 = strongest); negative = no sharpening.
    float sharpness = -1.0f;
    // Upscale with NVIDIA Image Scaling instead of FSR 1 where it can (<= 2x).
    // NIS sharpens while scaling, so RCAS is skipped after it.
    bool useNis = false;

    // Low-light boost. Off when maxGain <= 1.
    float lowLightTarget = 0.0f;
    float lowLightMaxGain = 1.0f;
    ID3D11ShaderResourceView* lumaStats = nullptr; // 1x1 smoothed mean luma

    // Background blur. Off when personMask is null.
    ID3D11ShaderResourceView* personMask = nullptr; // in source frame coordinates
    int blurPasses = 1;
};

// Builds the output frame on the GPU and copies it into a CPU buffer in the
// layout Media Foundation expects (NV12 or RGB32).
//
//   interpolate -> low-light boost -> FSR / NIS upscale -> background blur -> sharpen
//
// Each stage is skipped when it isn't needed.
class Compositor
{
public:
    HRESULT Init(const GpuDevice& gpu);
    HRESULT EnsureOutput(uint32_t width, uint32_t height, bool rgb32);

    void Compose(const FrameView& prev, const FrameView& next, const Texture* flowForward,
                 const Texture* flowBackward, const ComposeParams& params);

    // Blocks until the GPU finishes, then writes the frame. `pitch` is the row
    // stride of `dst` (for NV12 the chroma plane starts at dst + pitch * height).
    HRESULT Readback(uint8_t* dst, LONG pitch, DWORD capacity);

    // CPU time the last Readback spent copying pixels (after the GPU was done).
    double LastCopyMs() const { return m_lastCopyMs; }

    // Writes a flat dark frame (used before the first camera frame arrives).
    void FillBlank(uint8_t* dst, LONG pitch, DWORD capacity) const;

    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }

    // Size the last frame was rendered at before upscaling (== output size if
    // no upscaling happened).
    uint32_t RenderWidth() const { return m_renderW; }
    uint32_t RenderHeight() const { return m_renderH; }
    bool Upscaled() const { return m_renderW < m_width || m_renderH < m_height; }
    // Which upscaler built the last frame ("FSR 1" / "NIS"), if any.
    const wchar_t* UpscalerName() const { return !Upscaled() ? L"" : m_usedNis ? L"NIS" : L"FSR 1"; }

private:
    struct CompositeCb
    {
        uint32_t outW, outH;
        float srcW, srcH;
        float cropOffX, cropOffY;
        float cropScaleX, cropScaleY;
        float alpha;
        uint32_t mode;
        uint32_t compare;
        float confGain;
    };
    struct BgraCb
    {
        uint32_t w, h;
        uint32_t bt709;
        uint32_t pad;
    };
    struct EasuCb
    {
        uint32_t con[16];
        uint32_t outW, outH;
        uint32_t pad[2];
    };
    struct RcasCb
    {
        uint32_t con[4];
        uint32_t w, h;
        uint32_t pad[2];
    };
    struct NisCb
    {
        uint8_t data[256]; // NISConfig, filled by ComputeNisConstants
    };
    struct SizeCb
    {
        uint32_t w, h;
        uint32_t pad[2];
    };
    struct LowLightCb
    {
        uint32_t w, h;
        float target;
        float maxGain;
    };
    struct BgDownCb
    {
        uint32_t outW, outH;
        uint32_t inW, inH;
    };
    struct BlurCb
    {
        uint32_t w, h;
        int32_t dx, dy;
    };
    struct BgCompositeCb
    {
        uint32_t w, h;
        float cropOffX, cropOffY;
        float cropScaleX, cropScaleY;
        float edge0, edge1;
    };

    HRESULT EnsureRender(uint32_t w, uint32_t h);
    PlanarFrame& OtherOutput(const Texture* inUse);

    const GpuDevice* m_gpu = nullptr;
    ComPtr<ID3D11ComputeShader> m_composite, m_toBgra, m_easu, m_rcas, m_scaleUV, m_lowLight, m_bgDown, m_blur,
        m_bgComposite;
    ComPtr<ID3D11SamplerState> m_sampler;
    ConstantBuffer<CompositeCb> m_compositeCb;
    ConstantBuffer<BgraCb> m_bgraCb;
    ConstantBuffer<EasuCb> m_easuCb;
    ConstantBuffer<RcasCb> m_rcasCb;
    ConstantBuffer<NisCb> m_nisCb;
    ComPtr<ID3D11ComputeShader> m_nis;
    Texture m_nisCoefScale, m_nisCoefUsm;
    struct
    {
        uint32_t inW = 0, inH = 0, outW = 0, outH = 0;
        float sharpness = -1;
    } m_nisKey;
    ConstantBuffer<SizeCb> m_sizeCb;
    ConstantBuffer<LowLightCb> m_lowLightCb;
    ConstantBuffer<BgDownCb> m_bgDownCb;
    ConstantBuffer<BlurCb> m_blurCb;
    ConstantBuffer<BgCompositeCb> m_bgCompositeCb;

    PlanarFrame m_render[2]; // render size (camera pixels covering the output)
    PlanarFrame m_out[2];    // output size, ping-pong between stages
    Texture m_bgLow, m_bgTmp; // quarter-size background for the blur
    const Texture* m_finalY = nullptr;
    const Texture* m_finalUV = nullptr;

    Texture m_outBgra;
    Texture m_stageY, m_stageUV, m_stageBgra;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    uint32_t m_renderW = 0;
    uint32_t m_renderH = 0;
    bool m_rgb32 = false;
    double m_lastCopyMs = 0;
    bool m_usedNis = false;
};
} // namespace smoothcam
