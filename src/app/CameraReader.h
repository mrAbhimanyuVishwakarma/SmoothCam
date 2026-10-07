#pragma once

#include "Clock.h"

#include <windows.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <wil/com.h>

#include <cstdint>
#include <string>
#include <vector>

namespace smoothcam
{
// Opens the installed SmoothCam virtual camera exactly like a video app would
// (through Frame Server) and reads RGB32 (or NV12) frames from it. Used by the
// preview and by `SmoothCam.exe --probe`.
class CameraReader
{
public:
    struct Frame
    {
        std::vector<uint8_t> pixels; // top-down BGRA, or NV12 (Y plane then UV) when opened with nv12
        uint32_t width = 0;
        uint32_t height = 0;
        Hns timestamp = 0;
    };

    ~CameraReader() { Close(); }

    // Picks the mode closest to the requested width with the highest fps.
    // NV12 is what most video apps take; RGB32 is easier to draw.
    HRESULT Open(uint32_t preferredWidth, bool nv12 = false);
    void Close();

    // Blocks until the next frame arrives.
    HRESULT Read(Frame& frame);

    uint32_t Width() const { return m_width; }
    uint32_t Height() const { return m_height; }
    double Fps() const { return m_fps; }
    const std::wstring& Name() const { return m_name; }

private:
    wil::com_ptr_nothrow<IMFMediaSource> m_source;
    wil::com_ptr_nothrow<IMFSourceReader> m_reader;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    double m_fps = 0;
    LONG m_stride = 0;
    bool m_nv12 = false;
    std::wstring m_name;
};
} // namespace smoothcam
