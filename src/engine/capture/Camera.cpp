#include "Camera.h"

#include "Log.h"

#include <cfgmgr32.h>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h>
#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <wil/resource.h>
#include <wrl/implements.h>

#include <algorithm>
#include <cmath>
#include <cwctype>

namespace smoothcam
{
namespace
{
// The MF_SOURCE_READER_* stream constants are negative enum values.
constexpr DWORD kVideoStream = static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr DWORD kAllStreams = static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);

std::wstring ToLower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

std::wstring GetString(IMFActivate* activate, const GUID& key)
{
    wil::unique_cotaskmem_string value;
    UINT32 len = 0;
    if (FAILED(activate->GetAllocatedString(key, &value, &len)))
        return {};
    return value.get();
}

// Another virtual camera (a phone via Phone Link, OBS, ...) is just a COM media
// source that Windows registered under the camera's device interface. Inside
// Frame Server - where SmoothCam's own source runs - MFCreateDeviceSource can't
// open such a camera (E_NOINTERFACE), so create its media source directly, the
// same way Frame Server itself does.
HRESULT CreateVirtualCameraSource(const std::wstring& symbolicLink, ComPtr<IMFMediaSource>& source)
{
    HKEY rawKey = nullptr;
    if (CM_Open_Device_Interface_KeyW(symbolicLink.c_str(), KEY_READ, RegDisposition_OpenExisting, &rawKey, 0) !=
        CR_SUCCESS)
        return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    wil::unique_hkey key(rawKey);
    wchar_t text[64] = L"";
    DWORD size = sizeof(text);
    RETURN_IF_WIN32_ERROR(RegGetValueW(key.get(), nullptr, L"CustomCaptureSourceClsid", RRF_RT_REG_SZ, nullptr, text,
                                       &size));
    CLSID clsid{};
    RETURN_IF_FAILED(CLSIDFromString(text, &clsid));

    // Usually the class is an activator (IMFActivate); some hand out the source
    // itself. Give the activator what Frame Server would: the device's path.
    ComPtr<IMFActivate> activate;
    HRESULT hr = CoCreateInstance(clsid, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&activate));
    if (SUCCEEDED(hr))
    {
        // Copy everything Windows knows about the device (its enumeration entry)
        // onto the activator, then make sure the basics are there.
        ComPtr<IMFAttributes> filter;
        IMFActivate** devices = nullptr;
        UINT32 count = 0;
        if (SUCCEEDED(MFCreateAttributes(&filter, 1)) &&
            SUCCEEDED(filter->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)) &&
            SUCCEEDED(MFEnumDeviceSources(filter.get(), &devices, &count)))
        {
            for (UINT32 i = 0; i < count; ++i)
            {
                if (ToLower(GetString(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK)) ==
                    ToLower(symbolicLink))
                    devices[i]->CopyAllItems(activate.get());
                devices[i]->Release();
            }
            CoTaskMemFree(devices);
        }
        activate->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        activate->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, symbolicLink.c_str());
        hr = activate->ActivateObject(IID_PPV_ARGS(&source));
        if (FAILED(hr))
            SC_LOG_ERROR(L"Camera: virtual camera source %s refused to start (0x%08X)", text, hr);
        return hr;
    }
    hr = CoCreateInstance(clsid, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&source));
    if (FAILED(hr))
        SC_LOG_ERROR(L"Camera: can't create virtual camera source %s (0x%08X)", text, hr);
    return hr;
}

HRESULT CreateDeviceSource(const std::wstring& symbolicLink, ComPtr<IMFMediaSource>& source)
{
    ComPtr<IMFAttributes> attrs;
    RETURN_IF_FAILED(MFCreateAttributes(&attrs, 2));
    RETURN_IF_FAILED(attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
    RETURN_IF_FAILED(attrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, symbolicLink.c_str()));
    const HRESULT hr = MFCreateDeviceSource(attrs.get(), &source);
    if (FAILED(hr) && IsVirtualCamera(symbolicLink))
    {
        const HRESULT direct = CreateVirtualCameraSource(symbolicLink, source);
        if (SUCCEEDED(direct))
            return direct;
        SC_LOG_ERROR(L"Camera: opening virtual camera failed (0x%08X, direct 0x%08X)", hr, direct);
    }
    return hr;
}

HRESULT GetFirstVideoStreamHandler(IMFMediaSource* source, ComPtr<IMFPresentationDescriptor>& pd,
                                   ComPtr<IMFMediaTypeHandler>& handler)
{
    RETURN_IF_FAILED(source->CreatePresentationDescriptor(&pd));
    DWORD count = 0;
    RETURN_IF_FAILED(pd->GetStreamDescriptorCount(&count));
    for (DWORD i = 0; i < count; ++i)
    {
        BOOL selected = FALSE;
        ComPtr<IMFStreamDescriptor> sd;
        RETURN_IF_FAILED(pd->GetStreamDescriptorByIndex(i, &selected, &sd));
        ComPtr<IMFMediaTypeHandler> h;
        RETURN_IF_FAILED(sd->GetMediaTypeHandler(&h));
        GUID major{};
        if (SUCCEEDED(h->GetMajorType(&major)) && major == MFMediaType_Video)
        {
            handler = h;
            return S_OK;
        }
    }
    return MF_E_NOT_FOUND;
}

CameraFormat DescribeType(IMFMediaType* type)
{
    CameraFormat f;
    MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &f.width, &f.height);
    UINT32 num = 0, den = 0;
    if (SUCCEEDED(MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &num, &den)) && den)
        f.fps = static_cast<double>(num) / den;
    type->GetGUID(MF_MT_SUBTYPE, &f.subtype);
    return f;
}

bool IsUncompressed(const GUID& subtype)
{
    return subtype == MFVideoFormat_NV12 || subtype == MFVideoFormat_YUY2 || subtype == MFVideoFormat_RGB32 ||
           subtype == MFVideoFormat_I420 || subtype == MFVideoFormat_UYVY;
}

// Supported decode paths from the source reader with advanced video processing.
bool IsUsable(const GUID& subtype)
{
    return IsUncompressed(subtype) || subtype == MFVideoFormat_MJPG || subtype == MFVideoFormat_RGB24;
}

// Asks the camera to keep its frame rate instead of stretching exposure in low
// light (UVC "auto-exposure priority"). Best effort - many drivers ignore it.
void SetSteadyFrameRate(IMFMediaSource* source, bool steady)
{
    ComPtr<IKsControl> ks;
    if (FAILED(source->QueryInterface(IID_PPV_ARGS(&ks))))
        return;
    KSPROPERTY_CAMERACONTROL_S prop{};
    prop.Property.Set = PROPSETID_VIDCAP_CAMERACONTROL;
    prop.Property.Id = KSPROPERTY_CAMERACONTROL_AUTO_EXPOSURE_PRIORITY;
    prop.Property.Flags = KSPROPERTY_TYPE_SET;
    prop.Value = steady ? 0 : 1;
    prop.Flags = KSPROPERTY_CAMERACONTROL_FLAGS_MANUAL;
    ULONG returned = 0;
    const HRESULT hr = ks->KsProperty(&prop.Property, sizeof(prop), &prop, sizeof(prop), &returned);
    SC_LOG_VERBOSE(L"Camera: auto-exposure priority=%d -> 0x%08X", prop.Value, hr);
}
} // namespace

std::wstring SubtypeName(const GUID& subtype)
{
    if (subtype == MFVideoFormat_NV12) return L"NV12";
    if (subtype == MFVideoFormat_YUY2) return L"YUY2";
    if (subtype == MFVideoFormat_MJPG) return L"MJPG";
    if (subtype == MFVideoFormat_RGB32) return L"RGB32";
    if (subtype == MFVideoFormat_RGB24) return L"RGB24";
    if (subtype == MFVideoFormat_I420) return L"I420";
    if (subtype == MFVideoFormat_UYVY) return L"UYVY";
    if (subtype == MFVideoFormat_H264) return L"H264";
    wchar_t buf[16];
    // FourCC subtypes keep the code in Data1.
    const char* c = reinterpret_cast<const char*>(&subtype.Data1);
    swprintf_s(buf, L"%C%C%C%C", c[0], c[1], c[2], c[3]);
    return buf;
}

bool IsSmoothCamDevice(const std::wstring& name, const std::wstring& /*symbolicLink*/)
{
    // Windows names it "SmoothCam (Windows Virtual Camera)". Only our own camera
    // is excluded as a source: other virtual cameras - a phone connected with
    // Phone Link, OBS, ... - are perfectly good inputs.
    return ToLower(name).find(L"smoothcam") != std::wstring::npos;
}

bool IsVirtualCamera(const std::wstring& symbolicLink)
{
    // Cameras created with MFCreateVirtualCamera live under the VCAMDEVAPI enumerator.
    return ToLower(symbolicLink).find(L"vcamdevapi") != std::wstring::npos;
}

std::vector<CameraInfo> EnumerateAllCameras()
{
    std::vector<CameraInfo> result;
    ComPtr<IMFAttributes> attrs;
    if (FAILED(MFCreateAttributes(&attrs, 1)) ||
        FAILED(attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)))
        return result;

    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (FAILED(MFEnumDeviceSources(attrs.get(), &devices, &count)))
        return result;
    for (UINT32 i = 0; i < count; ++i)
    {
        CameraInfo info;
        info.name = GetString(devices[i], MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME);
        info.symbolicLink = GetString(devices[i], MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK);
        if (!info.symbolicLink.empty())
            result.push_back(std::move(info));
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    return result;
}

std::vector<CameraInfo> EnumerateCameras()
{
    auto all = EnumerateAllCameras();
    std::erase_if(all, [](const CameraInfo& c) { return IsSmoothCamDevice(c.name, c.symbolicLink); });
    // Real cameras first, so "first camera found" doesn't pick an idle virtual one.
    std::stable_partition(all.begin(), all.end(), [](const CameraInfo& c) { return !IsVirtualCamera(c.symbolicLink); });
    return all;
}

HRESULT EnumerateCameraFormats(const std::wstring& symbolicLink, std::vector<CameraFormat>& formats)
{
    formats.clear();
    ComPtr<IMFMediaSource> source;
    RETURN_IF_FAILED(CreateDeviceSource(symbolicLink, source));
    auto shutdown = wil::scope_exit([&] { source->Shutdown(); });

    ComPtr<IMFPresentationDescriptor> pd;
    ComPtr<IMFMediaTypeHandler> handler;
    RETURN_IF_FAILED(GetFirstVideoStreamHandler(source.get(), pd, handler));
    DWORD count = 0;
    RETURN_IF_FAILED(handler->GetMediaTypeCount(&count));
    for (DWORD i = 0; i < count; ++i)
    {
        ComPtr<IMFMediaType> type;
        if (SUCCEEDED(handler->GetMediaTypeByIndex(i, &type)))
            formats.push_back(DescribeType(type.get()));
    }
    return S_OK;
}

HRESULT QueryExposurePriority(const std::wstring& symbolicLink, long& value)
{
    ComPtr<IMFMediaSource> source;
    RETURN_IF_FAILED(CreateDeviceSource(symbolicLink, source));
    auto shutdown = wil::scope_exit([&] { source->Shutdown(); });
    ComPtr<IKsControl> ks;
    RETURN_IF_FAILED(source->QueryInterface(IID_PPV_ARGS(&ks)));
    KSPROPERTY_CAMERACONTROL_S prop{};
    prop.Property.Set = PROPSETID_VIDCAP_CAMERACONTROL;
    prop.Property.Id = KSPROPERTY_CAMERACONTROL_AUTO_EXPOSURE_PRIORITY;
    prop.Property.Flags = KSPROPERTY_TYPE_GET;
    ULONG returned = 0;
    RETURN_IF_FAILED(ks->KsProperty(&prop.Property, sizeof(prop), &prop, sizeof(prop), &returned));
    value = prop.Value;
    return S_OK;
}

size_t ChooseCameraFormat(const std::vector<CameraFormat>& formats, uint32_t wantWidth, uint32_t wantHeight,
                          double wantFps)
{
    const double wantAspect = wantHeight ? static_cast<double>(wantWidth) / wantHeight : 16.0 / 9.0;
    size_t best = SIZE_MAX;
    double bestScore = -1e18;
    for (size_t i = 0; i < formats.size(); ++i)
    {
        const auto& f = formats[i];
        if (!IsUsable(f.subtype) || f.width == 0 || f.height == 0)
            continue;

        // Priorities, most important first:
        //   1. resolution covers the request (no upscaling)
        //   2. frame rate, up to what we output - real frames beat made-up ones
        //   3. matching aspect ratio (less cropping)
        //   4. closest size (don't decode 4K for a 720p call)
        //   5. uncompressed over MJPEG (cheaper, no decode artifacts)
        const bool covers = f.width >= wantWidth && f.height >= wantHeight;
        const double fps = std::min(f.fps, wantFps);
        const double aspect = static_cast<double>(f.width) / f.height;
        const bool aspectMatch = std::abs(aspect - wantAspect) < 0.02;
        const double sizeDiff = std::abs(static_cast<double>(f.height) - wantHeight);

        double score = 0;
        score += covers ? 1e9 : f.height * 1e3; // if nothing covers, prefer the biggest
        score += std::round(fps) * 1e6;
        score += aspectMatch ? 5e5 : 0;
        score -= sizeDiff * 10;
        score += IsUncompressed(f.subtype) ? 1 : 0;
        if (score > bestScore)
        {
            bestScore = score;
            best = i;
        }
    }
    return best;
}

class CameraCapture::Callback
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          IMFSourceReaderCallback>
{
public:
    explicit Callback(FrameCallback cb) : m_callback(std::move(cb)) { m_flushed.create(); }

    void SetReader(IMFSourceReader* reader)
    {
        std::lock_guard lock(m_lock);
        m_reader = reader;
    }

    // Stops delivering frames; after this returns no callback will run.
    void Shutdown()
    {
        ComPtr<IMFSourceReader> reader;
        {
            std::lock_guard lock(m_lock);
            m_stopped = true;
            reader = m_reader;
        }
        if (reader && SUCCEEDED(reader->Flush(kAllStreams)))
            m_flushed.wait(2000);
        std::lock_guard lock(m_lock);
        m_reader = nullptr;
        m_callback = nullptr;
    }

    STDMETHODIMP OnReadSample(HRESULT status, DWORD, DWORD flags, LONGLONG timestamp, IMFSample* sample) override
    {
        std::lock_guard lock(m_lock);
        if (m_stopped || !m_callback)
            return S_OK;

        if (FAILED(status) || (flags & (MF_SOURCE_READERF_ERROR | MF_SOURCE_READERF_ENDOFSTREAM)))
        {
            m_callback(FAILED(status) ? status : MF_E_END_OF_STREAM, nullptr, 0);
            return S_OK;
        }
        if (sample)
            m_callback(S_OK, sample, timestamp);

        const HRESULT hr =
            m_reader->ReadSample(kVideoStream, 0, nullptr, nullptr, nullptr, nullptr);
        if (FAILED(hr))
            m_callback(hr, nullptr, 0);
        return S_OK;
    }

    STDMETHODIMP OnFlush(DWORD) override
    {
        m_flushed.SetEvent();
        return S_OK;
    }

    STDMETHODIMP OnEvent(DWORD, IMFMediaEvent*) override { return S_OK; }

private:
    std::mutex m_lock;
    FrameCallback m_callback;
    IMFSourceReader* m_reader = nullptr; // owned by CameraCapture
    bool m_stopped = false;
    wil::unique_event m_flushed;
};

CameraCapture::CameraCapture() = default;

CameraCapture::~CameraCapture()
{
    Close();
}

HRESULT CameraCapture::Open(const std::wstring& requestedLink, uint32_t wantWidth, uint32_t wantHeight,
                            double wantFps, IMFDXGIDeviceManager* manager, bool steadyFps, FrameCallback callback)
{
    Close();

    std::wstring link = requestedLink;
    m_name.clear();
    const auto cameras = EnumerateCameras();
    if (cameras.empty())
        return MF_E_NO_CAPTURE_DEVICES_AVAILABLE;
    auto it = std::find_if(cameras.begin(), cameras.end(),
                           [&](const CameraInfo& c) { return _wcsicmp(c.symbolicLink.c_str(), link.c_str()) == 0; });
    if (it == cameras.end())
    {
        if (!link.empty())
            SC_LOG_INFO(L"Camera: configured device not present, using '%s'", cameras.front().name.c_str());
        it = cameras.begin();
    }
    link = it->symbolicLink;
    m_name = it->name;

    RETURN_IF_FAILED(CreateDeviceSource(link, m_source));

    ComPtr<IMFPresentationDescriptor> pd;
    ComPtr<IMFMediaTypeHandler> handler;
    RETURN_IF_FAILED(GetFirstVideoStreamHandler(m_source.get(), pd, handler));

    DWORD count = 0;
    RETURN_IF_FAILED(handler->GetMediaTypeCount(&count));
    std::vector<CameraFormat> formats;
    std::vector<ComPtr<IMFMediaType>> types;
    for (DWORD i = 0; i < count; ++i)
    {
        ComPtr<IMFMediaType> type;
        if (SUCCEEDED(handler->GetMediaTypeByIndex(i, &type)))
        {
            formats.push_back(DescribeType(type.get()));
            types.push_back(type);
        }
    }
    const size_t choice = ChooseCameraFormat(formats, wantWidth, wantHeight, wantFps);
    RETURN_HR_IF(MF_E_INVALIDMEDIATYPE, choice == SIZE_MAX);
    RETURN_IF_FAILED(handler->SetCurrentMediaType(types[choice].get()));
    m_format = formats[choice];
    SC_LOG_INFO(L"Camera: '%s' %ux%u @ %.2f fps %s", m_name.c_str(), m_format.width, m_format.height, m_format.fps,
                SubtypeName(m_format.subtype).c_str());

    SetSteadyFrameRate(m_source.get(), steadyFps);

    m_callback = Microsoft::WRL::Make<Callback>(std::move(callback));
    RETURN_IF_NULL_ALLOC(m_callback);

    HRESULT hr = CreateReader(manager);
    if (FAILED(hr) && manager)
    {
        // Some drivers can't hand frames to D3D11; fall back to system memory.
        SC_LOG_INFO(L"Camera: GPU frame path failed (0x%08X), using system memory", hr);
        hr = CreateReader(nullptr);
    }
    if (FAILED(hr))
    {
        Close();
        return hr;
    }
    m_callback->SetReader(m_reader.get());
    hr = m_reader->ReadSample(kVideoStream, 0, nullptr, nullptr, nullptr, nullptr);
    if (FAILED(hr))
        Close();
    return hr;
}

HRESULT CameraCapture::CreateReader(IMFDXGIDeviceManager* manager)
{
    m_reader.reset();
    ComPtr<IMFAttributes> attrs;
    RETURN_IF_FAILED(MFCreateAttributes(&attrs, 6));
    RETURN_IF_FAILED(attrs->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, m_callback.get()));
    RETURN_IF_FAILED(attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE));
    RETURN_IF_FAILED(attrs->SetUINT32(MF_LOW_LATENCY, TRUE));
    // The reader must not shut down a source we created; Close() does that.
    RETURN_IF_FAILED(attrs->SetUINT32(MF_SOURCE_READER_DISCONNECT_MEDIASOURCE_ON_SHUTDOWN, TRUE));
    if (manager)
    {
        RETURN_IF_FAILED(attrs->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, manager));
        RETURN_IF_FAILED(attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE));
    }
    RETURN_IF_FAILED(MFCreateSourceReaderFromMediaSource(m_source.get(), attrs.get(), &m_reader));

    ComPtr<IMFMediaType> out;
    RETURN_IF_FAILED(MFCreateMediaType(&out));
    RETURN_IF_FAILED(out->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    RETURN_IF_FAILED(out->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
    RETURN_IF_FAILED(MFSetAttributeSize(out.get(), MF_MT_FRAME_SIZE, m_format.width, m_format.height));
    RETURN_IF_FAILED(m_reader->SetCurrentMediaType(kVideoStream, nullptr, out.get()));
    return S_OK;
}

void CameraCapture::Close()
{
    if (m_callback)
        m_callback->Shutdown();
    m_reader.reset();
    m_callback.reset();
    if (m_source)
    {
        m_source->Shutdown();
        m_source.reset();
    }
}
} // namespace smoothcam
