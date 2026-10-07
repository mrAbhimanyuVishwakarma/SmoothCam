// GPU tests for motion estimation and compositing, on synthetic frames where
// the true motion is known exactly. They run on whatever GPU the machine has
// (WARP in CI).

#include "compositor/Compositor.h"
#include "flow/Flow.h"
#include "flow/NvofFlow.h"
#include "gpu/Gpu.h"

#include <DirectXPackedVector.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

using namespace smoothcam;

namespace
{
constexpr uint32_t W = 1280;
constexpr uint32_t H = 720;
constexpr int kMargin = 96;

// Multi-octave value noise on a canvas larger than the frame, so frames can be
// cut out at different offsets to simulate camera/scene motion.
class Canvas
{
public:
    Canvas() : m_w(W + 2 * kMargin), m_h(H + 2 * kMargin), m_data(static_cast<size_t>(m_w) * m_h, 0.0f)
    {
        std::mt19937 rng(1234);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        const int cells[] = {6, 14, 32, 70};
        const float amps[] = {0.18f, 0.12f, 0.08f, 0.05f};
        for (int o = 0; o < 4; ++o)
        {
            const int gw = m_w / cells[o] + 2;
            const int gh = m_h / cells[o] + 2;
            std::vector<float> lattice(static_cast<size_t>(gw) * gh);
            for (auto& v : lattice)
                v = dist(rng);
            for (int y = 0; y < m_h; ++y)
            {
                for (int x = 0; x < m_w; ++x)
                {
                    const float fx = static_cast<float>(x) / cells[o];
                    const float fy = static_cast<float>(y) / cells[o];
                    const int ix = static_cast<int>(fx);
                    const int iy = static_cast<int>(fy);
                    float tx = fx - ix;
                    float ty = fy - iy;
                    tx = tx * tx * (3 - 2 * tx);
                    ty = ty * ty * (3 - 2 * ty);
                    const float a = lattice[iy * gw + ix];
                    const float b = lattice[iy * gw + ix + 1];
                    const float c = lattice[(iy + 1) * gw + ix];
                    const float d = lattice[(iy + 1) * gw + ix + 1];
                    m_data[static_cast<size_t>(y) * m_w + x] +=
                        amps[o] * ((a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty);
                }
            }
        }
    }

    // NV12 frame whose content has moved by (dx, dy) pixels (integers).
    std::vector<uint8_t> Frame(int dx, int dy) const
    {
        std::vector<uint8_t> nv12(static_cast<size_t>(W) * H * 3 / 2, 128);
        for (uint32_t y = 0; y < H; ++y)
        {
            for (uint32_t x = 0; x < W; ++x)
            {
                const float v = 0.5f + m_data[static_cast<size_t>(y + kMargin - dy) * m_w + (x + kMargin - dx)];
                nv12[static_cast<size_t>(y) * W + x] = static_cast<uint8_t>(std::clamp(16.0f + v * 219.0f, 16.0f, 235.0f));
            }
        }
        return nv12;
    }

private:
    int m_w, m_h;
    std::vector<float> m_data;
};

const Canvas& GetCanvas()
{
    static Canvas canvas;
    return canvas;
}

struct FieldStats
{
    float medianX = 0;
    float medianY = 0;
    float meanConf = 0;
};

class FlowTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // SMOOTHCAM_TEST_WARP=1 runs on the software rasterizer, like CI does.
        wchar_t warp[8] = L"";
        const bool forceWarp = GetEnvironmentVariableW(L"SMOOTHCAM_TEST_WARP", warp, 8) > 0 && warp[0] == L'1';
        ASSERT_HRESULT_SUCCEEDED(GpuDevice::Create(m_gpu, forceWarp));
        ASSERT_HRESULT_SUCCEEDED(m_pyramids.Init(*m_gpu));
        ASSERT_HRESULT_SUCCEEDED(m_refiner.Init(*m_gpu));
    }

    HRESULT Upload(const std::vector<uint8_t>& nv12, Nv12Texture& tex, LumaPyramid& pyr)
    {
        RETURN_IF_FAILED(tex.Create(m_gpu->Device(), W, H));
        m_gpu->Context()->UpdateSubresource(tex.tex.get(), 0, nullptr, nv12.data(), W, 0);
        RETURN_IF_FAILED(pyr.Ensure(m_gpu->Device(), W, H));
        m_pyramids.Build(m_gpu->Context(), tex.srvY.get(), pyr);
        return S_OK;
    }

    // Reads an RG16F or RGBA16F field and summarizes the interior cells.
    FieldStats Read(const Texture& field)
    {
        Texture staging;
        EXPECT_HRESULT_SUCCEEDED(staging.CreateStaging(m_gpu->Device(), field.width, field.height, field.format));
        auto* ctx = m_gpu->Context();
        ctx->CopyResource(staging.tex.get(), field.tex.get());
        D3D11_MAPPED_SUBRESOURCE m{};
        EXPECT_HRESULT_SUCCEEDED(ctx->Map(staging.tex.get(), 0, D3D11_MAP_READ, 0, &m));
        const int channels = field.format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 4 : 2;
        std::vector<float> xs, ys;
        double conf = 0;
        const uint32_t bx = field.width / 8;
        const uint32_t by = field.height / 8;
        for (uint32_t y = by; y < field.height - by; ++y)
        {
            const auto* row = reinterpret_cast<const uint16_t*>(static_cast<const uint8_t*>(m.pData) + y * m.RowPitch);
            for (uint32_t x = bx; x < field.width - bx; ++x)
            {
                xs.push_back(DirectX::PackedVector::XMConvertHalfToFloat(row[x * channels]));
                ys.push_back(DirectX::PackedVector::XMConvertHalfToFloat(row[x * channels + 1]));
                if (channels == 4)
                    conf += DirectX::PackedVector::XMConvertHalfToFloat(row[x * channels + 2]);
            }
        }
        ctx->Unmap(staging.tex.get(), 0);
        FieldStats s;
        std::nth_element(xs.begin(), xs.begin() + xs.size() / 2, xs.end());
        std::nth_element(ys.begin(), ys.begin() + ys.size() / 2, ys.end());
        s.medianX = xs[xs.size() / 2];
        s.medianY = ys[ys.size() / 2];
        s.meanConf = static_cast<float>(conf / xs.size());
        return s;
    }

    void RunPair(FlowEstimator& estimator, int dx, int dy, Texture& fwd, Texture& bwd, RawFlow& raw)
    {
        const auto& canvas = GetCanvas();
        ASSERT_HRESULT_SUCCEEDED(Upload(canvas.Frame(0, 0), m_a, m_pa));
        ASSERT_HRESULT_SUCCEEDED(Upload(canvas.Frame(dx, dy), m_b, m_pb));
        ASSERT_HRESULT_SUCCEEDED(estimator.Estimate(m_pa, m_pb, nullptr, raw));
        const auto& f = *raw.forward;
        ASSERT_HRESULT_SUCCEEDED(fwd.Create(m_gpu->Device(), f.width, f.height, DXGI_FORMAT_R16G16B16A16_FLOAT));
        ASSERT_HRESULT_SUCCEEDED(bwd.Create(m_gpu->Device(), f.width, f.height, DXGI_FORMAT_R16G16B16A16_FLOAT));
        const int l = estimator.WorkLevel();
        m_refiner.Run(m_gpu->Context(), *raw.forward, *raw.backward, m_pa.level[l], m_pb.level[l], W, H, fwd);
        m_refiner.Run(m_gpu->Context(), *raw.backward, *raw.forward, m_pb.level[l], m_pa.level[l], W, H, bwd);
    }

    double Psnr(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
    {
        double se = 0;
        size_t n = 0;
        for (uint32_t y = kMargin; y < H - kMargin; ++y)
        {
            for (uint32_t x = kMargin; x < W - kMargin; ++x)
            {
                const double d = static_cast<double>(a[y * W + x]) - b[y * W + x];
                se += d * d;
                ++n;
            }
        }
        const double mse = se / n;
        return mse <= 1e-9 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
    }

    std::vector<uint8_t> Compose(ComposeMode mode, float alpha, const Texture* fwd, const Texture* bwd)
    {
        Compositor comp;
        EXPECT_HRESULT_SUCCEEDED(comp.Init(*m_gpu));
        EXPECT_HRESULT_SUCCEEDED(comp.EnsureOutput(W, H, false));
        ComposeParams p;
        p.mode = mode;
        p.alpha = alpha;
        comp.Compose(m_a.View(), m_b.View(), fwd, bwd, p);
        std::vector<uint8_t> out(static_cast<size_t>(W) * H * 3 / 2);
        EXPECT_HRESULT_SUCCEEDED(comp.Readback(out.data(), W, static_cast<DWORD>(out.size())));
        return out;
    }

    std::unique_ptr<GpuDevice> m_gpu;
    PyramidBuilder m_pyramids;
    FlowRefiner m_refiner;
    Nv12Texture m_a, m_b;
    LumaPyramid m_pa, m_pb;
};
} // namespace

TEST_F(FlowTest, ComputeFlowFindsSmallTranslation)
{
    ComputeFlow flow(*m_gpu, 0);
    ASSERT_HRESULT_SUCCEEDED(flow.Init());
    Texture fwd, bwd;
    RawFlow raw;
    RunPair(flow, 6, -4, fwd, bwd, raw);

    const auto f = Read(fwd);
    const auto b = Read(bwd);
    EXPECT_NEAR(f.medianX, 6.0f, 0.5f);
    EXPECT_NEAR(f.medianY, -4.0f, 0.5f);
    EXPECT_NEAR(b.medianX, -6.0f, 0.5f);
    EXPECT_NEAR(b.medianY, 4.0f, 0.5f);
    EXPECT_GT(f.meanConf, 0.6f);
    EXPECT_GT(b.meanConf, 0.6f);
}

TEST_F(FlowTest, ComputeFlowFindsLargeTranslation)
{
    ComputeFlow flow(*m_gpu, 0);
    ASSERT_HRESULT_SUCCEEDED(flow.Init());
    Texture fwd, bwd;
    RawFlow raw;
    RunPair(flow, 40, 14, fwd, bwd, raw);
    const auto f = Read(fwd);
    EXPECT_NEAR(f.medianX, 40.0f, 1.0f);
    EXPECT_NEAR(f.medianY, 14.0f, 1.0f);
}

TEST_F(FlowTest, BalancedLevelStillFindsMotion)
{
    ComputeFlow flow(*m_gpu, 1);
    ASSERT_HRESULT_SUCCEEDED(flow.Init());
    Texture fwd, bwd;
    RawFlow raw;
    RunPair(flow, 10, 6, fwd, bwd, raw);
    const auto f = Read(fwd);
    EXPECT_NEAR(f.medianX, 10.0f, 1.0f);
    EXPECT_NEAR(f.medianY, 6.0f, 1.0f);
}

TEST_F(FlowTest, InterpolatedFrameMatchesTruthBetterThanBlend)
{
    ComputeFlow flow(*m_gpu, 0);
    ASSERT_HRESULT_SUCCEEDED(flow.Init());
    Texture fwd, bwd;
    RawFlow raw;
    RunPair(flow, 12, -8, fwd, bwd, raw);

    const auto truth = GetCanvas().Frame(6, -4);
    const double interp = Psnr(Compose(ComposeMode::Interpolate, 0.5f, &fwd, &bwd), truth);
    const double blend = Psnr(Compose(ComposeMode::Blend, 0.5f, nullptr, nullptr), truth);
    RecordProperty("psnr_interpolated", std::to_string(interp));
    RecordProperty("psnr_blend", std::to_string(blend));
    EXPECT_GT(interp, 32.0);
    EXPECT_GT(interp, blend + 5.0);
}

TEST_F(FlowTest, ExtrapolationContinuesMotion)
{
    ComputeFlow flow(*m_gpu, 0);
    ASSERT_HRESULT_SUCCEEDED(flow.Init());
    Texture fwd, bwd;
    RawFlow raw;
    RunPair(flow, 8, 4, fwd, bwd, raw);
    // Half an interval past B, content should be at (12, 6).
    const auto truth = GetCanvas().Frame(12, 6);
    const double extrap = Psnr(Compose(ComposeMode::Extrapolate, 0.5f, nullptr, &bwd), truth);
    const double hold = Psnr(GetCanvas().Frame(8, 4), truth);
    EXPECT_GT(extrap, hold + 5.0);
}

TEST_F(FlowTest, NvidiaOpticalFlowAgreesWithTruth)
{
    if (!m_gpu->Info().IsNvidia() || !IsNvofRuntimePresent())
        GTEST_SKIP() << "No NVIDIA Optical Flow on this machine";
    NvofFlow flow(*m_gpu, 0);
    if (FAILED(flow.Init()))
        GTEST_SKIP() << "NVIDIA Optical Flow failed to initialize";
    Texture fwd, bwd;
    RawFlow raw;
    RunPair(flow, 12, -8, fwd, bwd, raw);
    // Checks magnitude and the sign convention (NvofFlow::kSign).
    const auto f = Read(*raw.forward);
    const auto b = Read(*raw.backward);
    EXPECT_NEAR(f.medianX, 12.0f, 1.0f);
    EXPECT_NEAR(f.medianY, -8.0f, 1.0f);
    EXPECT_NEAR(b.medianX, -12.0f, 1.0f);
    EXPECT_NEAR(b.medianY, 8.0f, 1.0f);

    const auto truth = GetCanvas().Frame(6, -4);
    EXPECT_GT(Psnr(Compose(ComposeMode::Interpolate, 0.5f, &fwd, &bwd), truth), 32.0);
}

TEST_F(FlowTest, UpscalersBeatBilinear)
{
    // Camera at half the output resolution: box-downsample the test image 2x,
    // upscale it back and compare with the original.
    const auto truth = GetCanvas().Frame(0, 0);
    const uint32_t w = W / 2;
    const uint32_t h = H / 2;
    std::vector<uint8_t> lowRes(static_cast<size_t>(w) * h * 3 / 2, 128);
    for (uint32_t y = 0; y < h; ++y)
        for (uint32_t x = 0; x < w; ++x)
            lowRes[y * w + x] = static_cast<uint8_t>(
                (truth[(2 * y) * W + 2 * x] + truth[(2 * y) * W + 2 * x + 1] + truth[(2 * y + 1) * W + 2 * x] +
                 truth[(2 * y + 1) * W + 2 * x + 1] + 2) / 4);

    Nv12Texture src;
    ASSERT_HRESULT_SUCCEEDED(src.Create(m_gpu->Device(), w, h));
    m_gpu->Context()->UpdateSubresource(src.tex.get(), 0, nullptr, lowRes.data(), w, 0);

    Compositor comp;
    ASSERT_HRESULT_SUCCEEDED(comp.Init(*m_gpu));
    ASSERT_HRESULT_SUCCEEDED(comp.EnsureOutput(W, H, false));
    auto run = [&](float sharpness, Upscaler upscaler) {
        ComposeParams p;
        p.mode = ComposeMode::Hold;
        p.sharpness = sharpness;
        p.upscaler = upscaler;
        comp.Compose(src.View(), src.View(), nullptr, nullptr, p);
        std::vector<uint8_t> out(static_cast<size_t>(W) * H * 3 / 2);
        EXPECT_HRESULT_SUCCEEDED(comp.Readback(out.data(), W, static_cast<DWORD>(out.size())));
        return out;
    };

    // Reference: plain bilinear stretch on the CPU.
    std::vector<uint8_t> bilinear(truth.size(), 128);
    for (uint32_t y = 0; y < H; ++y)
    {
        for (uint32_t x = 0; x < W; ++x)
        {
            const float sx = std::clamp((x + 0.5f) / 2 - 0.5f, 0.0f, w - 1.0f);
            const float sy = std::clamp((y + 0.5f) / 2 - 0.5f, 0.0f, h - 1.0f);
            const uint32_t x0 = static_cast<uint32_t>(sx), y0 = static_cast<uint32_t>(sy);
            const uint32_t x1 = std::min(x0 + 1, w - 1), y1 = std::min(y0 + 1, h - 1);
            const float fx = sx - x0, fy = sy - y0;
            const float v = (lowRes[y0 * w + x0] * (1 - fx) + lowRes[y0 * w + x1] * fx) * (1 - fy) +
                            (lowRes[y1 * w + x0] * (1 - fx) + lowRes[y1 * w + x1] * fx) * fy;
            bilinear[y * W + x] = static_cast<uint8_t>(v + 0.5f);
        }
    }
    const double psnrBilinear = Psnr(bilinear, truth);
    RecordProperty("psnr_bilinear", std::to_string(psnrBilinear));
    std::printf("PSNR vs original: bilinear (CPU) %.2f dB", psnrBilinear);

    // Every upscaler beats (or matches) a plain stretch, and says which ran.
    const struct
    {
        Upscaler choice;
        const wchar_t* name;
        const char* key;
    } cases[] = {
        {Upscaler::Fsr, L"FSR 1", "psnr_fsr"},     {Upscaler::Nis, L"NIS", "psnr_nis"},
        {Upscaler::Cas, L"CAS", "psnr_cas"},       {Upscaler::Lanczos, L"Lanczos", "psnr_lanczos"},
        {Upscaler::Auto, L"CAS", "psnr_auto"}, // 2x: Auto picks CAS
    };
    for (const auto& c : cases)
    {
        const auto out = run(-1.0f, c.choice);
        EXPECT_TRUE(comp.Upscaled());
        EXPECT_EQ(comp.RenderWidth(), w);
        EXPECT_STREQ(comp.UpscalerName(), c.name);
        const double psnr = Psnr(out, truth);
        RecordProperty(c.key, std::to_string(psnr));
        std::printf(", %ls %.2f dB", c.name, psnr);
        EXPECT_GT(psnr, 28.0) << c.key;
        EXPECT_GE(psnr, psnrBilinear - 0.1) << c.key;

        // Sharpening on top stays sane (no ringing explosions or dark borders).
        const auto sharpened = run(0.0f, c.choice);
        EXPECT_GT(Psnr(sharpened, truth), 26.0) << c.key;
        EXPECT_GT(sharpened[(H / 2) * W + 2], 16) << c.key;
    }

    // Off: no upscaler at all, the frame is built straight at output size.
    const auto off = run(-1.0f, Upscaler::Off);
    EXPECT_FALSE(comp.Upscaled());
    EXPECT_STREQ(comp.UpscalerName(), L"");
    std::printf(", off %.2f dB\n", Psnr(off, truth));
    EXPECT_NEAR(Psnr(off, truth), psnrBilinear, 1.0);
}
