#pragma once

#include <cstdint>
#include <filesystem>

namespace smoothcam
{
// Prints to the parent console, or to stdout when it's redirected.
void Print(const wchar_t* format, ...);

// `SmoothCam.exe --diag`: versions, GPUs, cameras and registration state.
int RunDiagnostics();

// `SmoothCam.exe --probe [seconds] [--snap DIR]`: opens the installed virtual
// camera like a video app would and measures what comes out.
int RunProbe(int seconds, const std::filesystem::path& snapDir, uint32_t width = 1280, bool nv12 = false);
} // namespace smoothcam
