// smoothcam_bench - drive the engine with a real camera and report timing.
//
//   smoothcam_bench [--seconds N] [--size WxH] [--fps N] [--mode quality|balanced|lowlatency|off]
//                   [--engine auto|nvidia|gpu|basic] [--compare] [--snap DIR] [--list]
//                   [--sharpen S] [--noise N] [--lowlight L] [--background B] [--autoframe A]
//   smoothcam_bench --vision-image PICTURE --out DIR   (face detection + person mask on a still image)

#include "Engine.h"
#include "Log.h"
#include "Png.h"
#include "capture/Camera.h"
#include "vision/Vision.h"

#include <mfapi.h>

#include <wil/resource.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

using namespace smoothcam;

namespace
{
std::wstring g_exeDir;

struct Options
{
    int seconds = 8;
    OutputFormat format;
    Settings settings;
    std::wstring snapDir;
    bool list = false;
    std::wstring visionImage;
    std::wstring outDir;
};

bool Parse(int argc, wchar_t** argv, Options& o)
{
    for (int i = 1; i < argc; ++i)
    {
        std::wstring a = argv[i];
        auto next = [&]() -> std::wstring { return i + 1 < argc ? argv[++i] : L""; };
        if (a == L"--seconds")
            o.seconds = _wtoi(next().c_str());
        else if (a == L"--size")
        {
            const auto v = next();
            if (swscanf_s(v.c_str(), L"%ux%u", &o.format.width, &o.format.height) != 2)
                return false;
        }
        else if (a == L"--fps")
            o.format.fpsNum = static_cast<uint32_t>(_wtoi(next().c_str()));
        else if (a == L"--mode")
            o.settings.mode = ParseMode(next()).value_or(Mode::Quality);
        else if (a == L"--engine")
            o.settings.engine = ParseEngine(next()).value_or(EngineChoice::Auto);
        else if (a == L"--compare")
            o.settings.compare = true;
        else if (a == L"--snap")
            o.snapDir = next();
        else if (a == L"--list")
            o.list = true;
        else if (a == L"--sharpen")
            o.settings.sharpening = ParseSharpening(next()).value_or(Sharpening::Auto);
        else if (a == L"--noise")
            o.settings.noiseReduction = ParseNoiseReduction(next()).value_or(NoiseReduction::Off);
        else if (a == L"--lowlight")
            o.settings.lowLight = ParseLowLightBoost(next()).value_or(LowLightBoost::Off);
        else if (a == L"--background")
            o.settings.background = ParseBackground(next()).value_or(Background::Off);
        else if (a == L"--autoframe")
            o.settings.autoFrame = ParseAutoFrame(next()).value_or(AutoFrame::Off);
        else if (a == L"--vision-image")
            o.visionImage = next();
        else if (a == L"--out")
            o.outDir = next();
        else
            return false;
    }
    o.settings.logLevel = 1;
    return true;
}

void ListCameras()
{
    for (const auto& cam : EnumerateCameras())
    {
        wprintf(L"%s\n  %s\n", cam.name.c_str(), cam.symbolicLink.c_str());
        std::vector<CameraFormat> formats;
        if (SUCCEEDED(EnumerateCameraFormats(cam.symbolicLink, formats)))
            for (const auto& f : formats)
                wprintf(L"    %4ux%-4u %6.2f fps %s\n", f.width, f.height, f.fps, SubtypeName(f.subtype).c_str());
    }
}
// Runs face detection and person segmentation on a still image and writes
// mask.png and overlay.png (background dimmed, faces boxed) to the --out folder.
int RunVisionImage(const Options& o)
{
    std::vector<uint8_t> bgra;
    uint32_t w = 0, h = 0;
    if (FAILED(ReadImageBgra(o.visionImage, bgra, w, h)))
    {
        fwprintf(stderr, L"cannot read %s\n", o.visionImage.c_str());
        return 1;
    }
    std::vector<uint8_t> rgba(bgra.size()), gray(static_cast<size_t>(w) * h);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i)
    {
        rgba[i * 4 + 0] = bgra[i * 4 + 2];
        rgba[i * 4 + 1] = bgra[i * 4 + 1];
        rgba[i * 4 + 2] = bgra[i * 4 + 0];
        rgba[i * 4 + 3] = 255;
        gray[i] = static_cast<uint8_t>((bgra[i * 4 + 2] * 77 + bgra[i * 4 + 1] * 150 + bgra[i * 4 + 0] * 29) >> 8);
    }

    VisionWorker vision;
    vision.Configure(true, true, std::filesystem::path(g_exeDir) / L"models" / L"selfie_segmenter.onnx");
    std::vector<uint8_t> mask;
    uint32_t mw = 0, mh = 0;
    std::vector<FaceBox> faces;
    Hns facesTime = 0;
    const Hns start = QpcNow();
    bool gotMask = false;
    while (QpcNow() - start < 15 * kHnsPerSecond && (!gotMask || !facesTime))
    {
        vision.SubmitRgba(rgba.data(), w, h, w * 4);
        vision.SubmitLuma(gray.data(), w, h, w);
        Sleep(200);
        if (vision.TakeMask(mask, mw, mh))
            gotMask = true;
        faces = vision.Faces(facesTime);
    }
    wprintf(L"image %ux%u, mask %s (%ux%u), faces: %zu %s\n", w, h, gotMask ? L"yes" : L"NO", mw, mh, faces.size(),
            vision.Problem().c_str());
    for (const auto& f : faces)
        wprintf(L"  face at x=%.2f y=%.2f w=%.2f h=%.2f\n", f.x, f.y, f.w, f.h);
    if (!gotMask)
        return 1;

    double person = 0;
    for (uint8_t v : mask)
        person += v / 255.0;
    wprintf(L"person covers %.1f%% of the frame\n", 100.0 * person / mask.size());

    std::filesystem::create_directories(o.outDir);
    std::vector<uint8_t> maskImage(static_cast<size_t>(mw) * mh * 4);
    for (size_t i = 0; i < mask.size(); ++i)
    {
        maskImage[i * 4 + 0] = maskImage[i * 4 + 1] = maskImage[i * 4 + 2] = mask[i];
        maskImage[i * 4 + 3] = 255;
    }
    WritePngBgra(std::filesystem::path(o.outDir) / L"mask.png", maskImage.data(), mw, mh, mw * 4);

    std::vector<uint8_t> overlay = bgra;
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            const float m = mask[(y * mh / h) * mw + (x * mw / w)] / 255.0f;
            uint8_t* p = &overlay[(static_cast<size_t>(y) * w + x) * 4];
            for (int c = 0; c < 3; ++c)
                p[c] = static_cast<uint8_t>(p[c] * (0.25f + 0.75f * m));
        }
    }
    const uint8_t green[4] = {0, 255, 0, 255};
    for (const auto& f : faces)
    {
        const uint32_t x0 = static_cast<uint32_t>(f.x * w), y0 = static_cast<uint32_t>(f.y * h);
        const uint32_t x1 = std::min(w - 1, static_cast<uint32_t>((f.x + f.w) * w));
        const uint32_t y1 = std::min(h - 1, static_cast<uint32_t>((f.y + f.h) * h));
        for (uint32_t x = x0; x <= x1; ++x)
            for (uint32_t y : {y0, y1})
                memcpy(&overlay[(static_cast<size_t>(y) * w + x) * 4], green, 4);
        for (uint32_t y = y0; y <= y1; ++y)
            for (uint32_t x : {x0, x1})
                memcpy(&overlay[(static_cast<size_t>(y) * w + x) * 4], green, 4);
    }
    WritePngBgra(std::filesystem::path(o.outDir) / L"overlay.png", overlay.data(), w, h, w * 4);
    return 0;
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
    g_exeDir = std::filesystem::path(argv[0]).parent_path().wstring();
    if (g_exeDir.empty())
        g_exeDir = L".";
    Options o;
    if (!Parse(argc, argv, o))
    {
        fwprintf(stderr, L"usage: smoothcam_bench [--seconds N] [--size WxH] [--fps N] [--mode M] [--engine E] "
                         L"[--compare] [--snap DIR] [--list]\n");
        return 2;
    }

    auto co = wil::CoInitializeEx(COINIT_MULTITHREADED);
    MFStartup(MF_VERSION);
    log::Init(std::filesystem::temp_directory_path() / L"smoothcam-bench", L"bench", log::Level::Info);

    if (!o.visionImage.empty())
    {
        const int rc = RunVisionImage(o);
        MFShutdown();
        return rc;
    }

    if (o.list)
    {
        ListCameras();
        MFShutdown();
        return 0;
    }

    o.format.rgb32 = !o.snapDir.empty();
    Engine engine;
    HRESULT hr = engine.Start(o.format, o.settings);
    if (FAILED(hr))
    {
        fwprintf(stderr, L"engine start failed: 0x%08X\n", hr);
        return 1;
    }

    const uint32_t w = o.format.width;
    const uint32_t h = o.format.height;
    const LONG pitch = o.format.rgb32 ? static_cast<LONG>(w * 4) : static_cast<LONG>(w);
    std::vector<uint8_t> buffer(o.format.rgb32 ? w * h * 4 : w * h * 3 / 2);

    wil::unique_handle timer(CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                                    TIMER_ALL_ACCESS));
    const Hns period = o.format.FrameDuration();
    const Hns start = QpcNow();
    Hns nextTick = start;
    int frame = 0;
    int snapped = 0;
    int failures = 0;
    double worstMs = 0;
    Hns lastPrint = start;

    while (QpcNow() - start < o.seconds * kHnsPerSecond)
    {
        nextTick += period;
        const Hns wait = nextTick - QpcNow();
        if (wait > 0)
        {
            LARGE_INTEGER due;
            due.QuadPart = -wait;
            SetWaitableTimerEx(timer.get(), &due, 0, nullptr, nullptr, nullptr, 0);
            WaitForSingleObject(timer.get(), INFINITE);
        }

        const Hns t0 = QpcNow();
        hr = engine.Render(t0, buffer.data(), pitch, static_cast<DWORD>(buffer.size()));
        const double ms = HnsToMs(QpcNow() - t0);
        if (FAILED(hr))
            ++failures;
        if (t0 - start > 2 * kHnsPerSecond)
            worstMs = std::max(worstMs, ms);
        ++frame;

        // Save a run of consecutive frames once things have settled.
        if (!o.snapDir.empty() && t0 - start > 4 * kHnsPerSecond && snapped < 8)
        {
            std::filesystem::create_directories(o.snapDir);
            wchar_t name[64];
            swprintf_s(name, L"frame_%02d.png", snapped++);
            const HRESULT png = WritePngBgra(std::filesystem::path(o.snapDir) / name, buffer.data(), w, h, w * 4);
            if (FAILED(png))
                fwprintf(stderr, L"PNG write failed: 0x%08X\n", png);
        }

        if (t0 - lastPrint >= kHnsPerSecond)
        {
            lastPrint = t0;
            const auto s = engine.Stats();
            wprintf(L"in %5.2f fps | out %5.2f fps | +%5.1f ms | work %4.2f ms | %s | %s %s\n", s.inputFps,
                    s.outputFps, s.addedLatencyMs, s.gpuMs, s.flowEngine.c_str(), s.camera.c_str(),
                    s.message.c_str());
        }
    }

    const auto s = engine.Stats();
    wprintf(L"\nframes %d, failures %d, worst render %.2f ms, adapter: %s\n", frame, failures, worstMs,
            s.adapter.c_str());
    engine.Stop();
    MFShutdown();
    return failures == 0 ? 0 : 1;
}
