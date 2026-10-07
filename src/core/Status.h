#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace smoothcam
{
// Live numbers the media source publishes about once a second while an app is
// using the camera, so SmoothCam.exe can show them. Written to status.ini.
struct Status
{
    bool streaming = false;
    uint32_t processId = 0;   // Frame Server process hosting the source
    int64_t updatedAt = 0;    // FILETIME (UTC) of the last write
    double inputFps = 0;
    double outputFps = 0;
    double addedLatencyMs = 0;
    double gpuMs = 0;         // per output frame, including readback
    uint32_t width = 0;
    uint32_t height = 0;
    std::wstring upscale;     // "1280x720 -> 1920x1080" while upscaling
    std::wstring filters;     // active filters
    std::wstring engine;      // "NVIDIA Optical Flow", "GPU compute", "Basic"
    std::wstring mode;
    std::wstring camera;
    std::wstring adapter;
    std::wstring message;     // last notable event, e.g. "camera disconnected"

    bool Save(const std::filesystem::path& path) const;
    static Status Load(const std::filesystem::path& path);

    // True if the file was refreshed recently enough to trust "streaming".
    bool IsFresh(int64_t maxAgeMs = 3000) const;
};
} // namespace smoothcam
