#include "Status.h"

#include "Settings.h"

#include <windows.h>

#include <cstdio>

namespace smoothcam
{
namespace
{
int64_t NowFileTime()
{
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (static_cast<int64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

std::wstring Format(double v)
{
    wchar_t buf[32];
    swprintf_s(buf, L"%.2f", v);
    return buf;
}

double ToDouble(const KeyValues& kv, std::wstring_view key)
{
    auto it = kv.find(key);
    return it == kv.end() ? 0.0 : _wtof(it->second.c_str());
}

int64_t ToInt64(const KeyValues& kv, std::wstring_view key)
{
    auto it = kv.find(key);
    return it == kv.end() ? 0 : _wtoi64(it->second.c_str());
}

std::wstring ToString(const KeyValues& kv, std::wstring_view key)
{
    auto it = kv.find(key);
    return it == kv.end() ? std::wstring{} : it->second;
}
} // namespace

bool Status::Save(const std::filesystem::path& path) const
{
    KeyValues kv;
    kv[L"streaming"] = streaming ? L"1" : L"0";
    kv[L"pid"] = std::to_wstring(processId);
    kv[L"updated"] = std::to_wstring(NowFileTime());
    kv[L"input_fps"] = Format(inputFps);
    kv[L"output_fps"] = Format(outputFps);
    kv[L"latency_ms"] = Format(addedLatencyMs);
    kv[L"gpu_ms"] = Format(gpuMs);
    kv[L"width"] = std::to_wstring(width);
    kv[L"height"] = std::to_wstring(height);
    kv[L"engine"] = engine;
    kv[L"upscale"] = upscale;
    kv[L"filters"] = filters;
    kv[L"mode"] = mode;
    kv[L"camera"] = camera;
    kv[L"adapter"] = adapter;
    kv[L"message"] = message;
    return WriteKeyValueFile(path, kv, "SmoothCam live status - written by the virtual camera");
}

Status Status::Load(const std::filesystem::path& path)
{
    Status s;
    KeyValues kv;
    if (!ReadKeyValueFile(path, kv))
        return s;
    s.streaming = ToString(kv, L"streaming") == L"1";
    s.processId = static_cast<uint32_t>(ToInt64(kv, L"pid"));
    s.updatedAt = ToInt64(kv, L"updated");
    s.inputFps = ToDouble(kv, L"input_fps");
    s.outputFps = ToDouble(kv, L"output_fps");
    s.addedLatencyMs = ToDouble(kv, L"latency_ms");
    s.gpuMs = ToDouble(kv, L"gpu_ms");
    s.width = static_cast<uint32_t>(ToInt64(kv, L"width"));
    s.height = static_cast<uint32_t>(ToInt64(kv, L"height"));
    s.engine = ToString(kv, L"engine");
    s.upscale = ToString(kv, L"upscale");
    s.filters = ToString(kv, L"filters");
    s.mode = ToString(kv, L"mode");
    s.camera = ToString(kv, L"camera");
    s.adapter = ToString(kv, L"adapter");
    s.message = ToString(kv, L"message");
    return s;
}

bool Status::IsFresh(int64_t maxAgeMs) const
{
    const int64_t age = NowFileTime() - updatedAt; // 100 ns units
    return updatedAt != 0 && age >= 0 && age <= maxAgeMs * 10'000;
}
} // namespace smoothcam
