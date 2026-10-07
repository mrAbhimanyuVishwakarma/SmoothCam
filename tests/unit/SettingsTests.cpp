#include "Settings.h"
#include "Status.h"

#include <gtest/gtest.h>

#include <windows.h>

using namespace smoothcam;

namespace
{
std::filesystem::path TempFile(const wchar_t* name)
{
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    return std::filesystem::path(dir) / L"smoothcam-tests" / name;
}
} // namespace

TEST(Settings, DefaultsWhenMissing)
{
    const auto s = Settings::Load(TempFile(L"does-not-exist.ini"));
    EXPECT_EQ(s, Settings{});
    EXPECT_EQ(s.outputFps, 60u);
    EXPECT_EQ(s.mode, Mode::Quality);
}

TEST(Settings, RoundTrip)
{
    Settings s;
    s.device = L"\\\\?\\usb#vid_046d&pid_085e#{e5323777-f976-4f5b-9b55-b94699c46e44}\\global";
    s.deviceName = L"Caméra intégrée";
    s.maxWidth = 1920;
    s.maxHeight = 1080;
    s.outputFps = 50;
    s.cameraFps = 30;
    s.outputWidth = 2560;
    s.outputHeight = 1440;
    s.sharpening = Sharpening::Medium;
    s.noiseReduction = NoiseReduction::High;
    s.lowLight = LowLightBoost::Auto;
    s.background = Background::StrongBlur;
    s.autoFrame = AutoFrame::Close;
    s.upscaler = Upscaler::Lanczos;
    s.enabled = false;
    s.mode = Mode::LowLatency;
    s.engine = EngineChoice::Gpu;
    s.steadyFps = false;
    s.compare = true;
    s.logLevel = 2;

    const auto path = TempFile(L"roundtrip.ini");
    ASSERT_TRUE(s.Save(path));
    EXPECT_EQ(Settings::Load(path), s);
}

TEST(Settings, ParsesLooseInput)
{
    const auto kv = ParseKeyValues("\xEF\xBB\xBF# comment\r\n[section]\r\n  mode = Balanced \r\nengine=CPU\nbad line\n");
    EXPECT_EQ(kv.at(L"mode"), L"Balanced");
    EXPECT_EQ(ParseMode(kv.at(L"mode")), Mode::Balanced);
    EXPECT_EQ(ParseEngine(kv.at(L"engine")), EngineChoice::Basic);
    EXPECT_EQ(kv.count(L"bad line"), 0u);
}

TEST(Settings, ClampsFrameRate)
{
    EXPECT_EQ(ClampOutputFps(60), 60u);
    EXPECT_EQ(ClampOutputFps(144), 60u);
    EXPECT_EQ(ClampOutputFps(29), 30u);
    EXPECT_EQ(ClampOutputFps(47), 48u);
    EXPECT_EQ(ClampOutputFps(25), 25u);
    EXPECT_EQ(ClampOutputFps(0), 24u);
}

TEST(Settings, ClampsOutputHeight)
{
    EXPECT_EQ(ClampOutputHeight(0), 0u);
    EXPECT_EQ(ClampOutputHeight(720), 720u);
    EXPECT_EQ(ClampOutputHeight(500), 480u);
    EXPECT_EQ(ClampOutputHeight(1000), 1080u);
    EXPECT_EQ(ClampOutputHeight(1440), 1440u);
    EXPECT_EQ(ClampOutputHeight(4320), 2160u);
    EXPECT_EQ(ParseSharpening(L" High "), Sharpening::High);
    EXPECT_FALSE(ParseSharpening(L"extreme").has_value());
    EXPECT_EQ(ParseBackground(L"strong_blur"), Background::StrongBlur);
    EXPECT_EQ(ParseAutoFrame(L"Wide"), AutoFrame::Wide);
    EXPECT_EQ(ParseUpscaler(L"NIS"), Upscaler::Nis);
    EXPECT_EQ(ParseUpscaler(L"lanczos"), Upscaler::Lanczos);
    EXPECT_FALSE(ParseUpscaler(L"xbr").has_value());
    EXPECT_EQ(ParseLowLightBoost(L"strong"), LowLightBoost::Strong);
    EXPECT_EQ(ParseNoiseReduction(L"medium"), NoiseReduction::Medium);
}

TEST(Settings, SwitchedOffIsPlainCamera)
{
    Settings s;
    s.maxWidth = 1920;
    s.maxHeight = 1080;
    s.cameraFps = 30;
    s.outputFps = 60;
    s.outputWidth = 3840;
    s.outputHeight = 2160;
    s.noiseReduction = NoiseReduction::High;
    s.background = Background::Blur;
    s.compare = true;
    uint32_t w = 0, h = 0;
    EffectiveOutputSize(s, w, h);
    EXPECT_EQ(w, 3840u);
    EXPECT_EQ(EffectiveOutputFps(s), 60u);
    EXPECT_EQ(ProcessingSettings(s), s);

    s.enabled = false;
    EffectiveOutputSize(s, w, h);
    EXPECT_EQ(w, 1920u);
    EXPECT_EQ(h, 1080u);
    EXPECT_EQ(EffectiveOutputFps(s), 30u);
    const Settings p = ProcessingSettings(s);
    EXPECT_EQ(p.mode, Mode::Off);
    EXPECT_EQ(p.noiseReduction, NoiseReduction::Off);
    EXPECT_EQ(p.background, Background::Off);
    EXPECT_EQ(p.upscaler, Upscaler::Off);
    EXPECT_FALSE(p.compare);
}

TEST(Settings, EffectiveOutputSize)
{
    Settings s;
    uint32_t w = 0, h = 0;
    EffectiveOutputSize(s, w, h);
    EXPECT_EQ(w, 1280u);
    EXPECT_EQ(h, 720u);
    s.maxWidth = 1920;
    s.maxHeight = 1080;
    EffectiveOutputSize(s, w, h);
    EXPECT_EQ(w, 1920u);
    EXPECT_EQ(h, 1080u);
    s.outputWidth = 640;
    s.outputHeight = 480;
    EffectiveOutputSize(s, w, h);
    EXPECT_EQ(w, 640u);
    EXPECT_EQ(h, 480u);
    EXPECT_EQ(WidthForHeight(1440), 2560u);
    EXPECT_EQ(WidthForHeight(480), 854u);
}

TEST(Status, RoundTripAndFreshness)
{
    Status st;
    st.streaming = true;
    st.inputFps = 29.97;
    st.outputFps = 60;
    st.addedLatencyMs = 38.5;
    st.engine = L"GPU compute";
    const auto path = TempFile(L"status.ini");
    ASSERT_TRUE(st.Save(path));
    const auto loaded = Status::Load(path);
    EXPECT_TRUE(loaded.streaming);
    EXPECT_NEAR(loaded.inputFps, 29.97, 0.01);
    EXPECT_EQ(loaded.engine, L"GPU compute");
    EXPECT_TRUE(loaded.IsFresh());
}
