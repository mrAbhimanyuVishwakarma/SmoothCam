#include "Diagnostics.h"

#include "CameraReader.h"
#include "Paths.h"
#include "Png.h"
#include "Registration.h"
#include "Settings.h"
#include "Status.h"
#include "Version.h"
#include "capture/Camera.h"
#include "flow/NvofFlow.h"

#include <dxgi1_6.h>
#include <mfapi.h>

#include <wil/com.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace smoothcam
{
namespace
{
HANDLE OutputHandle()
{
    static HANDLE handle = [] {
        HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
        if (h && h != INVALID_HANDLE_VALUE && GetFileType(h) != FILE_TYPE_UNKNOWN)
            return h;
        // GUI-subsystem exe started from a terminal: borrow the terminal.
        if (AttachConsole(ATTACH_PARENT_PROCESS))
            return CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        return INVALID_HANDLE_VALUE;
    }();
    return handle;
}

std::wstring WindowsBuild()
{
    HKEY key;
    wchar_t display[64] = L"";
    DWORD build = 0;
    wchar_t buildText[16] = L"";
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", 0, KEY_READ, &key) ==
        ERROR_SUCCESS)
    {
        DWORD size = sizeof(display);
        RegQueryValueExW(key, L"DisplayVersion", nullptr, nullptr, reinterpret_cast<BYTE*>(display), &size);
        size = sizeof(buildText);
        RegQueryValueExW(key, L"CurrentBuildNumber", nullptr, nullptr, reinterpret_cast<BYTE*>(buildText), &size);
        RegCloseKey(key);
        build = static_cast<DWORD>(_wtoi(buildText));
    }
    return std::wstring(display) + L" (build " + std::to_wstring(build) + L")";
}
} // namespace

void Print(const wchar_t* format, ...)
{
    wchar_t text[2048];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(text, _TRUNCATE, format, args);
    va_end(args);

    HANDLE out = OutputHandle();
    if (out == INVALID_HANDLE_VALUE)
        return;
    DWORD mode = 0;
    DWORD written = 0;
    if (GetConsoleMode(out, &mode))
    {
        WriteConsoleW(out, text, static_cast<DWORD>(wcslen(text)), &written, nullptr);
        return;
    }
    char utf8[4096];
    const int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (len > 1)
        WriteFile(out, utf8, static_cast<DWORD>(len - 1), &written, nullptr);
}

int RunDiagnostics()
{
    Print(L"SmoothCam %s\n", SMOOTHCAM_VERSION_WSTRING);
    Print(L"Windows   %s\n", WindowsBuild().c_str());
    Print(L"Elevated  %s\n\n", IsElevated() ? L"yes" : L"no");

    Print(L"GPUs\n");
    wil::com_ptr_nothrow<IDXGIFactory6> factory;
    if (SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
    {
        wil::com_ptr_nothrow<IDXGIAdapter1> adapter;
        for (UINT i = 0;
             SUCCEEDED(factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)));
             ++i)
        {
            DXGI_ADAPTER_DESC1 d{};
            adapter->GetDesc1(&d);
            Print(L"  %s%s (vendor 0x%04X, %llu MB)\n", i == 0 ? L"* " : L"  ", d.Description, d.VendorId,
                  static_cast<unsigned long long>(d.DedicatedVideoMemory / (1024 * 1024)));
            adapter.reset();
        }
    }
    Print(L"  NVIDIA Optical Flow runtime: %s\n\n", IsNvofRuntimePresent() ? L"present" : L"not present");

    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    Print(L"Cameras\n");
    for (const auto& cam : EnumerateAllCameras())
    {
        const bool ours = IsSmoothCamDevice(cam.name, cam.symbolicLink);
        Print(L"  %s%s\n", cam.name.c_str(),
              ours ? L"  [SmoothCam]" : IsVirtualCamera(cam.symbolicLink) ? L"  [virtual - usable as input]" : L"");
        if (ours)
            continue;
        long priority = 0;
        if (SUCCEEDED(QueryExposurePriority(cam.symbolicLink, priority)))
            Print(L"      steady-fps control: supported (currently %s)\n",
                  priority == 0 ? L"frame rate kept" : L"may slow down in low light");
        else
            Print(L"      steady-fps control: not supported by this camera's driver\n");
        std::vector<CameraFormat> formats;
        if (SUCCEEDED(EnumerateCameraFormats(cam.symbolicLink, formats)))
        {
            std::sort(formats.begin(), formats.end(), [](const auto& a, const auto& b) {
                return a.width * a.height != b.width * b.height ? a.width * a.height > b.width * b.height : a.fps > b.fps;
            });
            for (const auto& f : formats)
                Print(L"      %4ux%-4u %6.2f fps  %s\n", f.width, f.height, f.fps, SubtypeName(f.subtype).c_str());
        }
    }

    const auto reg = QueryRegistration();
    Print(L"\nRegistration\n");
    Print(L"  Media source : %s %s\n", reg.comRegistered ? L"registered" : L"NOT registered", reg.dllPath.c_str());
    Print(L"  Camera       : %s\n", reg.cameraPresent ? reg.cameraName.c_str() : L"NOT present");

    const auto settings = Settings::Load(SettingsPath());
    Print(L"\nSettings (%s)\n", SettingsPath().c_str());
    Print(L"  camera %s, %u fps, mode %s, engine %s, steady %d, compare %d\n",
          settings.deviceName.empty() ? L"(first available)" : settings.deviceName.c_str(), settings.outputFps,
          ToString(settings.mode), ToString(settings.engine), settings.steadyFps, settings.compare);

    const auto status = Status::Load(StatusPath());
    Print(L"\nLive status: ");
    if (status.streaming && status.IsFresh())
        Print(L"in use, %ux%u, in %.2f fps -> out %.2f fps, +%.1f ms, GPU %.2f ms, %s\n", status.width,
              status.height, status.inputFps, status.outputFps, status.addedLatencyMs, status.gpuMs,
              status.engine.c_str());
    else
        Print(L"not in use\n");
    MFShutdown();
    return reg.comRegistered && reg.cameraPresent ? 0 : 1;
}

namespace
{
// BT.709 limited range. Approximate; only used to save probe snapshots.
std::vector<uint8_t> Nv12ToBgra(const std::vector<uint8_t>& nv12, uint32_t w, uint32_t h)
{
    std::vector<uint8_t> out(static_cast<size_t>(w) * h * 4);
    const uint8_t* uv = nv12.data() + static_cast<size_t>(w) * h;
    auto clamp = [](float v) { return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f)); };
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
        {
            const float Y = (nv12[static_cast<size_t>(y) * w + x] - 16) * 1.164f;
            const uint8_t* c = uv + static_cast<size_t>(y / 2) * w + (x & ~1u);
            const float U = c[0] - 128.0f;
            const float V = c[1] - 128.0f;
            uint8_t* p = out.data() + (static_cast<size_t>(y) * w + x) * 4;
            p[0] = clamp(Y + 2.112f * U);
            p[1] = clamp(Y - 0.213f * U - 0.533f * V);
            p[2] = clamp(Y + 1.793f * V);
            p[3] = 255;
        }
    }
    return out;
}
} // namespace

int RunProbe(int seconds, const std::filesystem::path& snapDir, uint32_t width, bool nv12)
{
    MFStartup(MF_VERSION);
    CameraReader reader;
    HRESULT hr = reader.Open(width, nv12);
    if (FAILED(hr))
    {
        Print(L"Could not open the SmoothCam camera (0x%08X). Is it installed? Try --diag.\n", hr);
        MFShutdown();
        return 1;
    }
    Print(L"Opened %s at %ux%u, %.2f fps (%s)\n", reader.Name().c_str(), reader.Width(), reader.Height(),
          reader.Fps(), nv12 ? L"NV12" : L"RGB32");

    CameraReader::Frame frame;
    std::vector<double> intervals;
    Hns firstArrival = 0;
    Hns lastArrival = 0;
    int frames = 0;
    int saved = 0;
    while (true)
    {
        hr = reader.Read(frame);
        if (FAILED(hr))
        {
            Print(L"Read failed: 0x%08X\n", hr);
            break;
        }
        const Hns now = QpcNow();
        if (!firstArrival)
            firstArrival = now;
        else
            intervals.push_back(HnsToMs(now - lastArrival));
        lastArrival = now;
        ++frames;

        // Skip the first two seconds (camera warm-up), then save a burst.
        if (!snapDir.empty() && now - firstArrival > 2 * kHnsPerSecond && saved < 6)
        {
            std::filesystem::create_directories(snapDir);
            wchar_t name[32];
            swprintf_s(name, L"probe_%02d.png", saved++);
            if (nv12)
            {
                const auto bgra = Nv12ToBgra(frame.pixels, frame.width, frame.height);
                WritePngBgra(snapDir / name, bgra.data(), frame.width, frame.height, frame.width * 4);
            }
            else
            {
                WritePngBgra(snapDir / name, frame.pixels.data(), frame.width, frame.height, frame.width * 4);
            }
        }
        if (now - firstArrival >= seconds * kHnsPerSecond)
            break;
    }
    reader.Close();
    MFShutdown();

    if (intervals.size() < 10)
    {
        Print(L"Too few frames (%d).\n", frames);
        return 1;
    }
    // Ignore the warm-up second.
    const size_t skip = std::min<size_t>(intervals.size() / 4, static_cast<size_t>(reader.Fps()));
    std::vector<double> steady(intervals.begin() + static_cast<ptrdiff_t>(skip), intervals.end());
    double sum = 0;
    for (double v : steady)
        sum += v;
    const double mean = sum / steady.size();
    double var = 0;
    for (double v : steady)
        var += (v - mean) * (v - mean);
    std::sort(steady.begin(), steady.end());
    const double p99 = steady[static_cast<size_t>(steady.size() * 0.99)];
    Print(L"Frames %d in %d s: %.2f fps, interval %.2f ms (jitter %.2f ms, p99 %.2f ms)\n", frames, seconds,
          1000.0 / mean, mean, std::sqrt(var / steady.size()), p99);

    const auto status = Status::Load(StatusPath());
    if (status.IsFresh(5000))
        Print(L"Engine: %s, input %.2f fps, +%.1f ms, GPU %.2f ms%s%s %s\n", status.engine.c_str(), status.inputFps,
              status.addedLatencyMs, status.gpuMs, status.upscale.empty() ? L"" : L", upscaling ",
              status.upscale.c_str(), status.message.c_str());
    return 0;
}
} // namespace smoothcam
