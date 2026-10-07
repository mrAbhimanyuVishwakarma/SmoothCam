#pragma once

#include "Clock.h"
#include "Settings.h"

#include <windows.h>

#include <memory>
#include <string>

namespace smoothcam
{
struct OutputFormat
{
    uint32_t width = 1280;
    uint32_t height = 720;
    uint32_t fpsNum = 60;
    uint32_t fpsDen = 1;
    bool rgb32 = false; // false = NV12

    Hns FrameDuration() const { return smoothcam::FrameDuration(fpsNum, fpsDen); }
};

struct EngineStats
{
    bool cameraOpen = false;
    double inputFps = 0;
    double outputFps = 0;
    double addedLatencyMs = 0;
    double gpuMs = 0;
    std::wstring flowEngine; // what actually runs, after fallbacks
    std::wstring filters;    // active filters, e.g. "noise reduction, auto-framing"
    std::wstring upscale;    // e.g. "1280x720 -> 1920x1080", empty when not upscaling
    std::wstring camera;
    std::wstring adapter;
    std::wstring message;
};

// The whole pipeline: camera -> motion estimation -> paced output frames.
//
// Thread model: the engine runs its own camera thread; Render() is called by
// the owner (the virtual camera's output clock) and does all GPU work.
class Engine
{
public:
    Engine();
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    HRESULT Start(const OutputFormat& format, const Settings& settings);
    void Stop();

    // Mode, engine choice and compare apply on the next frame. A different
    // camera or steady-fps setting reopens the camera.
    void UpdateSettings(const Settings& settings);

    // Produces the frame for presentation time `outputTime` (MF system clock)
    // into `dst`. For NV12 the chroma plane follows at dst + pitch * height.
    HRESULT Render(Hns outputTime, uint8_t* dst, LONG pitch, DWORD capacity);

    EngineStats Stats() const;
    OutputFormat Format() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace smoothcam
