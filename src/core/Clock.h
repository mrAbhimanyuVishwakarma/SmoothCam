#pragma once

#include <windows.h>

#include <cstdint>

namespace smoothcam
{
// All timestamps in SmoothCam use Media Foundation units: 100 ns ticks on the
// QPC clock, the same clock MFGetSystemTime() reads.
using Hns = int64_t;

inline constexpr Hns kHnsPerSecond = 10'000'000;
inline constexpr Hns kHnsPerMs = 10'000;

inline Hns QpcNow() noexcept
{
    static const int64_t freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f.QuadPart;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    // Split to avoid overflow for long uptimes.
    const int64_t whole = now.QuadPart / freq;
    const int64_t part = now.QuadPart % freq;
    return whole * kHnsPerSecond + part * kHnsPerSecond / freq;
}

inline double HnsToMs(Hns v) noexcept
{
    return static_cast<double>(v) / kHnsPerMs;
}

inline Hns FrameDuration(uint32_t fpsNum, uint32_t fpsDen) noexcept
{
    return fpsNum ? static_cast<Hns>(static_cast<int64_t>(fpsDen) * kHnsPerSecond / fpsNum) : 0;
}
} // namespace smoothcam
