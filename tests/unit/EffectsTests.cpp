// Tests for the effects stages (low-light boost, background blur) on the GPU,
// and for the auto-framing controller.

#include "compositor/Compositor.h"
#include "gpu/Gpu.h"
#include "vision/AutoFramer.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace smoothcam;

namespace
{
constexpr uint32_t W = 640;
constexpr uint32_t H = 360;

class EffectsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        wchar_t warp[8] = L"";
        const bool forceWarp = GetEnvironmentVariableW(L"SMOOTHCAM_TEST_WARP", warp, 8) > 0 && warp[0] == L'1';
        ASSERT_HRESULT_SUCCEEDED(GpuDevice::Create(m_gpu, forceWarp));
        ASSERT_HRESULT_SUCCEEDED(m_comp.Init(*m_gpu));
        ASSERT_HRESULT_SUCCEEDED(m_comp.EnsureOutput(W, H, false));
    }

    void Upload(const std::vector<uint8_t>& nv12)
    {
        ASSERT_HRESULT_SUCCEEDED(m_frame.Create(m_gpu->Device(), W, H));
        m_gpu->Context()->UpdateSubresource(m_frame.tex.get(), 0, nullptr, nv12.data(), W, 0);
    }

    std::vector<uint8_t> Run(const ComposeParams& p)
    {
        m_comp.Compose(m_frame.View(), m_frame.View(), nullptr, nullptr, p);
        std::vector<uint8_t> out(static_cast<size_t>(W) * H * 3 / 2);
        EXPECT_HRESULT_SUCCEEDED(m_comp.Readback(out.data(), W, static_cast<DWORD>(out.size())));
        return out;
    }

    std::unique_ptr<GpuDevice> m_gpu;
    Compositor m_comp;
    Nv12Texture m_frame;
};

// Sum of absolute horizontal differences in a region: a simple sharpness measure.
double Detail(const std::vector<uint8_t>& y, uint32_t x0, uint32_t x1)
{
    double sum = 0;
    for (uint32_t row = 40; row < H - 40; ++row)
        for (uint32_t x = x0 + 1; x < x1; ++x)
            sum += std::abs(static_cast<int>(y[row * W + x]) - static_cast<int>(y[row * W + x - 1]));
    return sum;
}
} // namespace

TEST_F(EffectsTest, LowLightBoostBrightensDarkFrames)
{
    // Uniform dark frame: luma 40 in limited range (about 11% brightness).
    std::vector<uint8_t> nv12(static_cast<size_t>(W) * H * 3 / 2, 128);
    std::fill(nv12.begin(), nv12.begin() + W * H, static_cast<uint8_t>(40));
    Upload(nv12);

    Texture stats;
    ASSERT_HRESULT_SUCCEEDED(stats.Create(m_gpu->Device(), 1, 1, DXGI_FORMAT_R32_FLOAT));
    const float mean = 40.0f / 255.0f;
    m_gpu->Context()->UpdateSubresource(stats.tex.get(), 0, nullptr, &mean, 4, 0);

    ComposeParams p;
    p.lowLightTarget = 0.40f;
    p.lowLightMaxGain = 3.0f;
    p.lumaStats = stats.srv.get();
    const auto out = Run(p);

    // Gain clamps at 3: v = 0.11 -> 0.11*3 / (1 + 2*0.11) = 0.27 -> luma ~75.
    EXPECT_NEAR(out[(H / 2) * W + W / 2], 75, 4);

    // Off means untouched.
    const auto off = Run(ComposeParams{});
    EXPECT_NEAR(off[(H / 2) * W + W / 2], 40, 1);
}

TEST_F(EffectsTest, LowLightBoostLeavesBrightFramesAlone)
{
    std::vector<uint8_t> nv12(static_cast<size_t>(W) * H * 3 / 2, 128);
    std::fill(nv12.begin(), nv12.begin() + W * H, static_cast<uint8_t>(150));
    Upload(nv12);
    Texture stats;
    ASSERT_HRESULT_SUCCEEDED(stats.Create(m_gpu->Device(), 1, 1, DXGI_FORMAT_R32_FLOAT));
    const float mean = 150.0f / 255.0f;
    m_gpu->Context()->UpdateSubresource(stats.tex.get(), 0, nullptr, &mean, 4, 0);
    ComposeParams p;
    p.lowLightTarget = 0.40f;
    p.lowLightMaxGain = 3.0f;
    p.lumaStats = stats.srv.get();
    EXPECT_NEAR(Run(p)[(H / 2) * W + W / 2], 150, 2);
}

TEST_F(EffectsTest, BackgroundBlurOnlyTouchesTheBackground)
{
    // Fine vertical stripes everywhere.
    std::vector<uint8_t> nv12(static_cast<size_t>(W) * H * 3 / 2, 128);
    for (uint32_t y = 0; y < H; ++y)
        for (uint32_t x = 0; x < W; ++x)
            nv12[y * W + x] = (x / 2) % 2 ? 200 : 40;
    Upload(nv12);

    // Person on the left half, background on the right.
    const uint32_t mw = 256, mh = 144;
    std::vector<uint8_t> mask(mw * mh);
    for (uint32_t y = 0; y < mh; ++y)
        for (uint32_t x = 0; x < mw; ++x)
            mask[y * mw + x] = x < mw / 2 ? 255 : 0;
    Texture maskTex;
    ASSERT_HRESULT_SUCCEEDED(maskTex.Create(m_gpu->Device(), mw, mh, DXGI_FORMAT_R8_UNORM, false));
    m_gpu->Context()->UpdateSubresource(maskTex.tex.get(), 0, nullptr, mask.data(), mw, 0);

    const auto sharp = Run(ComposeParams{});
    ComposeParams p;
    p.personMask = maskTex.srv.get();
    const auto blurred = Run(p);

    const double personBefore = Detail(sharp, 20, W / 2 - 40);
    const double personAfter = Detail(blurred, 20, W / 2 - 40);
    const double bgBefore = Detail(sharp, W / 2 + 40, W - 20);
    const double bgAfter = Detail(blurred, W / 2 + 40, W - 20);
    EXPECT_NEAR(personAfter, personBefore, personBefore * 0.02); // person stays sharp
    EXPECT_LT(bgAfter, bgBefore * 0.1);                          // background loses its detail
}

TEST(AutoFramer, ZoomsTowardsTheFaceAndBackOut)
{
    AutoFramer framer;
    framer.SetMode(AutoFrame::Close);
    const CropRect base{0, 0, 1, 1};
    const float aspect = 16.0f / 9.0f;
    // A face right of center, 20% of the frame tall.
    const std::vector<FaceBox> faces = {{0.60f, 0.30f, 0.12f, 0.20f}};

    Hns t = 0;
    CropRect crop{};
    for (int i = 0; i < 180; ++i) // 3 s at 60 fps
    {
        t += kHnsPerSecond / 60;
        crop = framer.Update(faces, t, t, base, aspect, aspect);
    }
    // Zoomed in (crop smaller than the frame), centered near the face, inside the frame.
    EXPECT_LT(crop.h, 0.6f);
    EXPECT_NEAR(crop.w / crop.h, 1.0f, 0.01f); // same aspect as source/output
    EXPECT_NEAR(crop.x + crop.w / 2, 0.66f, 0.06f);
    EXPECT_GE(crop.x, 0.0f);
    EXPECT_LE(crop.x + crop.w, 1.0f + 1e-5f);
    // Never more than the Close-up limit of 2.5x.
    EXPECT_GE(crop.h, 1.0f / 2.5f - 1e-3f);

    // Face lost: after a few seconds it's back to the full view.
    const Hns lastSeen = t;
    for (int i = 0; i < 300; ++i)
    {
        t += kHnsPerSecond / 60;
        crop = framer.Update({}, lastSeen, t, base, aspect, aspect);
    }
    EXPECT_NEAR(crop.h, 1.0f, 0.02f);
}

TEST(AutoFramer, IgnoresSmallHeadMovements)
{
    AutoFramer framer;
    framer.SetMode(AutoFrame::Wide);
    const CropRect base{0, 0, 1, 1};
    Hns t = 0;
    CropRect settled{};
    for (int i = 0; i < 240; ++i)
    {
        t += kHnsPerSecond / 60;
        settled = framer.Update({{0.45f, 0.35f, 0.10f, 0.18f}}, t, t, base, 16.0f / 9, 16.0f / 9);
    }
    // Nudge the face by a hair: the framing must not chase it.
    CropRect after = settled;
    for (int i = 0; i < 120; ++i)
    {
        t += kHnsPerSecond / 60;
        after = framer.Update({{0.455f, 0.352f, 0.10f, 0.18f}}, t, t, base, 16.0f / 9, 16.0f / 9);
    }
    EXPECT_NEAR(after.x, settled.x, 1e-3f);
    EXPECT_NEAR(after.y, settled.y, 1e-3f);
}
