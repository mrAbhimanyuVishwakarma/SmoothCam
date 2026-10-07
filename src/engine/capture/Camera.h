#pragma once

#include "Clock.h"
#include "gpu/Gpu.h"

#include <mfidl.h>
#include <mfreadwrite.h>

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace smoothcam
{
struct CameraInfo
{
    std::wstring name;
    std::wstring symbolicLink;
};

struct CameraFormat
{
    uint32_t width = 0;
    uint32_t height = 0;
    double fps = 0;
    GUID subtype{};
};

// Physical video capture devices. SmoothCam itself (and other Windows virtual
// cameras) are left out - picking SmoothCam as its own input would loop.
std::vector<CameraInfo> EnumerateCameras();
// Every video capture device, virtual ones included.
std::vector<CameraInfo> EnumerateAllCameras();
bool IsSmoothCamDevice(const std::wstring& name, const std::wstring& symbolicLink);
HRESULT EnumerateCameraFormats(const std::wstring& symbolicLink, std::vector<CameraFormat>& formats);

// Reads the UVC "auto-exposure priority" control: 0 = keep frame rate, 1 = may
// slow down in low light. Fails if the driver doesn't expose it.
HRESULT QueryExposurePriority(const std::wstring& symbolicLink, long& value);

// Picks the native mode that best serves a requested output size/rate.
// Exposed for unit tests.
size_t ChooseCameraFormat(const std::vector<CameraFormat>& formats, uint32_t wantWidth, uint32_t wantHeight,
                          double wantFps);

std::wstring SubtypeName(const GUID& subtype);

// Asynchronous capture from one camera, delivering NV12 samples (D3D11
// textures when a device manager is supplied).
class CameraCapture
{
public:
    // Called on a Media Foundation worker thread. `sample` is null on errors.
    using FrameCallback = std::function<void(HRESULT hr, IMFSample* sample, Hns deviceTime)>;

    CameraCapture();
    ~CameraCapture();
    CameraCapture(const CameraCapture&) = delete;
    CameraCapture& operator=(const CameraCapture&) = delete;

    HRESULT Open(const std::wstring& symbolicLink, uint32_t wantWidth, uint32_t wantHeight, double wantFps,
                 IMFDXGIDeviceManager* manager, bool steadyFps, FrameCallback callback);
    void Close();
    bool IsOpen() const { return m_reader != nullptr; }

    uint32_t Width() const { return m_format.width; }
    uint32_t Height() const { return m_format.height; }
    double NativeFps() const { return m_format.fps; }
    const std::wstring& Name() const { return m_name; }
    const CameraFormat& Format() const { return m_format; }

private:
    class Callback;
    HRESULT CreateReader(IMFDXGIDeviceManager* manager);

    ComPtr<IMFMediaSource> m_source;
    ComPtr<IMFSourceReader> m_reader;
    ComPtr<Callback> m_callback;
    CameraFormat m_format;
    std::wstring m_name;
};
} // namespace smoothcam
