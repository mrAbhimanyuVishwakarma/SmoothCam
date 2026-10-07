#include "MediaStream.h"

#include "Log.h"
#include "Paths.h"
#include "Status.h"

#include <mfapi.h>
#include <mferror.h>
#include <ks.h>
#include <ksmedia.h>

#include <wil/result.h>

#include <algorithm>
#include <vector>

namespace smoothcam
{
namespace
{
struct Resolution
{
    uint32_t width;
    uint32_t height;
};

// Standard 16:9 heights, largest first.
constexpr uint32_t kHeights[] = {2160, 1440, 1080, 720, 480, 360};

HRESULT CreateVideoType(const GUID& subtype, uint32_t width, uint32_t height, uint32_t fps,
                        wil::com_ptr_nothrow<IMFMediaType>& out)
{
    wil::com_ptr_nothrow<IMFMediaType> type;
    RETURN_IF_FAILED(MFCreateMediaType(&type));
    RETURN_IF_FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    RETURN_IF_FAILED(type->SetGUID(MF_MT_SUBTYPE, subtype));
    RETURN_IF_FAILED(MFSetAttributeSize(type.get(), MF_MT_FRAME_SIZE, width, height));
    RETURN_IF_FAILED(MFSetAttributeRatio(type.get(), MF_MT_FRAME_RATE, fps, 1));
    RETURN_IF_FAILED(MFSetAttributeRatio(type.get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1));
    RETURN_IF_FAILED(type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
    RETURN_IF_FAILED(type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
    RETURN_IF_FAILED(type->SetUINT32(MF_MT_FIXED_SIZE_SAMPLES, TRUE));
    const bool rgb = subtype == MFVideoFormat_RGB32;
    const uint32_t stride = rgb ? width * 4 : width;
    const uint32_t size = rgb ? width * height * 4 : width * height * 3 / 2;
    RETURN_IF_FAILED(type->SetUINT32(MF_MT_DEFAULT_STRIDE, stride));
    RETURN_IF_FAILED(type->SetUINT32(MF_MT_SAMPLE_SIZE, size));
    // 4K RGB32 at 60 fps overflows 32 bits; the attribute is only a hint.
    const uint64_t bitrate = static_cast<uint64_t>(size) * 8 * fps;
    RETURN_IF_FAILED(type->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(std::min<uint64_t>(bitrate, UINT32_MAX))));
    out = std::move(type);
    return S_OK;
}

OutputFormat ParseFormat(IMFMediaType* type)
{
    OutputFormat f;
    MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &f.width, &f.height);
    MFGetAttributeRatio(type, MF_MT_FRAME_RATE, &f.fpsNum, &f.fpsDen);
    if (!f.fpsNum || !f.fpsDen)
    {
        f.fpsNum = 30;
        f.fpsDen = 1;
    }
    GUID subtype{};
    type->GetGUID(MF_MT_SUBTYPE, &subtype);
    f.rgb32 = subtype == MFVideoFormat_RGB32;
    return f;
}

bool SameFormat(const OutputFormat& a, const OutputFormat& b)
{
    return a.width == b.width && a.height == b.height && a.fpsNum == b.fpsNum && a.fpsDen == b.fpsDen &&
           a.rgb32 == b.rgb32;
}
} // namespace

MediaStream::~MediaStream()
{
    Shutdown();
}

HRESULT MediaStream::RuntimeClassInitialize(IMFMediaSource* source, DWORD streamId, const Settings& settings)
{
    m_source = source;
    m_id = streamId;
    m_settings = settings;

    RETURN_IF_FAILED(MFCreateAttributes(&m_attributes, 4));
    RETURN_IF_FAILED(m_attributes->SetGUID(MF_DEVICESTREAM_STREAM_CATEGORY, PINNAME_VIDEO_CAPTURE));
    RETURN_IF_FAILED(m_attributes->SetUINT32(MF_DEVICESTREAM_STREAM_ID, streamId));
    RETURN_IF_FAILED(m_attributes->SetUINT32(MF_DEVICESTREAM_FRAMESERVER_SHARED, 1));
    RETURN_IF_FAILED(m_attributes->SetUINT32(MF_DEVICESTREAM_ATTRIBUTE_FRAMESOURCE_TYPES, MFFrameSourceTypes_Color));

    RETURN_IF_FAILED(MFCreateEventQueue(&m_queue));
    return BuildMediaTypes(settings);
}

HRESULT MediaStream::BuildMediaTypes(const Settings& settings)
{
    // The resolution and frame rate the user picked come first, so that's what
    // apps get by default. Smaller sizes follow for apps that want less, but
    // nothing larger and no other frame rate: earlier builds also offered a
    // 30 fps fallback and some apps happily picked it over the user's 60.
    uint32_t width = 0, height = 0;
    EffectiveOutputSize(settings, width, height);
    const uint32_t fps = EffectiveOutputFps(settings);
    std::vector<Resolution> resolutions = {{width, height}};
    for (uint32_t h : kHeights)
        if (h < height && WidthForHeight(h) < width)
            resolutions.push_back({WidthForHeight(h), h});
    // 4:3 VGA for old apps that insist on it.
    if (height > 480 && !(width == 640 && height == 480))
        resolutions.push_back({640, 480});

    std::vector<wil::com_ptr_nothrow<IMFMediaType>> types;
    for (const auto& r : resolutions)
    {
        for (const GUID& subtype : {MFVideoFormat_NV12, MFVideoFormat_RGB32})
        {
            wil::com_ptr_nothrow<IMFMediaType> type;
            RETURN_IF_FAILED(CreateVideoType(subtype, r.width, r.height, fps, type));
            types.push_back(std::move(type));
        }
    }

    std::vector<IMFMediaType*> raw;
    for (auto& t : types)
        raw.push_back(t.get());
    RETURN_IF_FAILED(MFCreateStreamDescriptor(m_id, static_cast<DWORD>(raw.size()), raw.data(), &m_descriptor));
    RETURN_IF_FAILED(m_attributes->CopyAllItems(m_descriptor.get()));

    wil::com_ptr_nothrow<IMFMediaTypeHandler> handler;
    RETURN_IF_FAILED(m_descriptor->GetMediaTypeHandler(&handler));
    RETURN_IF_FAILED(handler->SetCurrentMediaType(raw[0]));
    m_type = types[0];
    SC_LOG_INFO(L"MediaStream: %zu media types, default %ux%u @ %u fps", types.size(), resolutions[0].width,
                resolutions[0].height, fps);
    return S_OK;
}

// ---- IMFMediaEventGenerator ------------------------------------------------

STDMETHODIMP MediaStream::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state)
{
    std::lock_guard g(m_lock);
    RETURN_HR_IF(MF_E_SHUTDOWN, m_shutdown);
    return m_queue->BeginGetEvent(callback, state);
}

STDMETHODIMP MediaStream::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event)
{
    std::lock_guard g(m_lock);
    RETURN_HR_IF(MF_E_SHUTDOWN, m_shutdown);
    return m_queue->EndGetEvent(result, event);
}

STDMETHODIMP MediaStream::GetEvent(DWORD flags, IMFMediaEvent** event)
{
    wil::com_ptr_nothrow<IMFMediaEventQueue> queue;
    {
        std::lock_guard g(m_lock);
        RETURN_HR_IF(MF_E_SHUTDOWN, m_shutdown);
        queue = m_queue;
    }
    return queue->GetEvent(flags, event);
}

STDMETHODIMP MediaStream::QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* val)
{
    std::lock_guard g(m_lock);
    RETURN_HR_IF(MF_E_SHUTDOWN, m_shutdown);
    return m_queue->QueueEventParamVar(type, extendedType, status, val);
}

// ---- IMFMediaStream ---------------------------------------------------------

STDMETHODIMP MediaStream::GetMediaSource(IMFMediaSource** source)
{
    RETURN_HR_IF_NULL(E_POINTER, source);
    std::lock_guard g(m_lock);
    RETURN_HR_IF(MF_E_SHUTDOWN, m_shutdown);
    *source = m_source;
    m_source->AddRef();
    return S_OK;
}

STDMETHODIMP MediaStream::GetStreamDescriptor(IMFStreamDescriptor** descriptor)
{
    RETURN_HR_IF_NULL(E_POINTER, descriptor);
    std::lock_guard g(m_lock);
    RETURN_HR_IF(MF_E_SHUTDOWN, m_shutdown);
    return m_descriptor.copy_to(descriptor);
}

STDMETHODIMP MediaStream::RequestSample(IUnknown* token)
{
    std::lock_guard g(m_lock);
    RETURN_HR_IF(MF_E_SHUTDOWN, m_shutdown);
    RETURN_HR_IF(MF_E_INVALIDREQUEST, m_state != MF_STREAM_STATE_RUNNING);
    // Answered by the clock thread on its next tick.
    m_requests.emplace_back(token);
    return S_OK;
}

// ---- IMFMediaStream2 --------------------------------------------------------

STDMETHODIMP MediaStream::SetStreamState(MF_STREAM_STATE state)
{
    MF_STREAM_STATE current;
    wil::com_ptr_nothrow<IMFMediaType> type;
    {
        std::lock_guard g(m_lock);
        RETURN_HR_IF(MF_E_SHUTDOWN, m_shutdown);
        current = m_state;
        type = m_type;
    }
    if (current == state)
        return S_OK;
    switch (state)
    {
    case MF_STREAM_STATE_RUNNING:
        return Start(type.get());
    case MF_STREAM_STATE_STOPPED:
        return Stop(true);
    case MF_STREAM_STATE_PAUSED: {
        std::lock_guard g(m_lock);
        RETURN_HR_IF(MF_E_INVALID_STATE_TRANSITION, m_state != MF_STREAM_STATE_RUNNING);
        m_state = MF_STREAM_STATE_PAUSED;
        return S_OK;
    }
    default:
        return MF_E_INVALID_STATE_TRANSITION;
    }
}

STDMETHODIMP MediaStream::GetStreamState(MF_STREAM_STATE* state)
{
    RETURN_HR_IF_NULL(E_POINTER, state);
    std::lock_guard g(m_lock);
    *state = m_state;
    return S_OK;
}

// ---- control ----------------------------------------------------------------

HRESULT MediaStream::Start(IMFMediaType* type)
{
    RETURN_HR_IF_NULL(E_INVALIDARG, type);
    const OutputFormat format = ParseFormat(type);
    bool clockRunning = false;
    {
        std::lock_guard g(m_lock);
        RETURN_HR_IF(MF_E_SHUTDOWN, m_shutdown);
        if (!m_allocator)
            RETURN_IF_FAILED(MFCreateVideoSampleAllocatorEx(IID_PPV_ARGS(&m_allocator)));
        m_allocator->UninitializeSampleAllocator();
        RETURN_IF_FAILED(m_allocator->InitializeSampleAllocator(8, type));
        m_type = type;
        clockRunning = m_clock.joinable();
    }

    // A format change restarts the engine; the same format just resumes.
    if (clockRunning && !SameFormat(format, m_format))
    {
        m_stopClock.SetEvent();
        m_clock.join();
        m_engine.Stop();
        clockRunning = false;
    }
    if (!clockRunning)
    {
        m_format = format;
        m_settings = Settings::Load(SettingsPath());
        RETURN_IF_FAILED(m_engine.Start(format, m_settings));
        m_stopClock.ResetEvent();
        m_clock = std::thread([this] { ClockLoop(); });
    }

    std::lock_guard g(m_lock);
    m_state = MF_STREAM_STATE_RUNNING;
    SC_LOG_INFO(L"MediaStream: started %ux%u @ %u/%u %s", format.width, format.height, format.fpsNum, format.fpsDen,
                format.rgb32 ? L"RGB32" : L"NV12");
    return m_queue->QueueEventParamVar(MEStreamStarted, GUID_NULL, S_OK, nullptr);
}

HRESULT MediaStream::Stop(bool sendEvent)
{
    {
        std::lock_guard g(m_lock);
        if (m_shutdown)
            return MF_E_SHUTDOWN;
        m_state = MF_STREAM_STATE_STOPPED;
        m_requests.clear();
    }
    if (m_clock.joinable())
    {
        m_stopClock.SetEvent();
        m_clock.join();
    }
    m_engine.Stop();

    std::lock_guard g(m_lock);
    if (m_allocator)
        m_allocator->UninitializeSampleAllocator();
    SC_LOG_INFO(L"MediaStream: stopped after %llu frames", m_delivered);
    return sendEvent ? m_queue->QueueEventParamVar(MEStreamStopped, GUID_NULL, S_OK, nullptr) : S_OK;
}

void MediaStream::Shutdown()
{
    {
        std::lock_guard g(m_lock);
        if (m_shutdown)
            return;
    }
    Stop(false);
    std::lock_guard g(m_lock);
    m_shutdown = true;
    if (m_queue)
        m_queue->Shutdown();
    m_allocator.reset();
    m_source = nullptr;
}

HRESULT MediaStream::SetAllocator(IUnknown* allocator)
{
    std::lock_guard g(m_lock);
    m_allocator.reset();
    return allocator->QueryInterface(IID_PPV_ARGS(&m_allocator));
}

HRESULT MediaStream::GetAttributes(IMFAttributes** attributes)
{
    std::lock_guard g(m_lock);
    return m_attributes.copy_to(attributes);
}

bool MediaStream::IsActive()
{
    std::lock_guard g(m_lock);
    return m_state != MF_STREAM_STATE_STOPPED;
}

// ---- output clock -------------------------------------------------------------

void MediaStream::ClockLoop()
{
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    SetThreadDescription(GetCurrentThread(), L"SmoothCam clock");

    wil::unique_handle timer(
        CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS));
    const Hns period = m_format.FrameDuration();
    Hns next = QpcNow();
    m_lastStatus = 0;

    for (;;)
    {
        next += period;
        const Hns now = QpcNow();
        if (next < now - 2 * period)
            next = now; // fell far behind (system sleep, debugger) - resync
        const Hns wait = next - now;
        if (wait > 0 && timer)
        {
            LARGE_INTEGER due;
            due.QuadPart = -wait;
            SetWaitableTimerEx(timer.get(), &due, 0, nullptr, nullptr, nullptr, 0);
            HANDLE handles[] = {m_stopClock.get(), timer.get()};
            if (WaitForMultipleObjects(2, handles, FALSE, INFINITE) == WAIT_OBJECT_0)
                break;
        }
        else if (m_stopClock.is_signaled())
        {
            break;
        }

        PollSettings();
        DeliverFrame(next);

        if (QpcNow() - m_lastStatus >= kHnsPerSecond)
        {
            m_lastStatus = QpcNow();
            PublishStatus(true);
        }
    }
    PublishStatus(false);
    if (SUCCEEDED(co))
        CoUninitialize();
}

void MediaStream::DeliverFrame(Hns tickTime)
{
    wil::com_ptr_nothrow<IUnknown> token;
    wil::com_ptr_nothrow<IMFVideoSampleAllocatorEx> allocator;
    {
        std::lock_guard g(m_lock);
        if (m_state != MF_STREAM_STATE_RUNNING || m_requests.empty() || !m_allocator)
            return;
        token = std::move(m_requests.front());
        m_requests.pop_front();
        allocator = m_allocator;
    }

    wil::com_ptr_nothrow<IMFSample> sample;
    HRESULT hr = allocator->AllocateSample(&sample);
    if (FAILED(hr))
    {
        // Pool exhausted (the app is holding on to samples) - try next tick.
        std::lock_guard g(m_lock);
        m_requests.push_front(std::move(token));
        return;
    }

    wil::com_ptr_nothrow<IMFMediaBuffer> buffer;
    if (SUCCEEDED(sample->GetBufferByIndex(0, &buffer)))
    {
        wil::com_ptr_nothrow<IMF2DBuffer2> buffer2d;
        BYTE* scan0 = nullptr;
        BYTE* start = nullptr;
        LONG pitch = 0;
        DWORD length = 0;
        if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer2d))) &&
            SUCCEEDED(buffer2d->Lock2DSize(MF2DBuffer_LockFlags_Write, &scan0, &pitch, &start, &length)))
        {
            const DWORD capacity = length - static_cast<DWORD>(scan0 - start);
            hr = m_engine.Render(QpcNow(), scan0, pitch, capacity);
            buffer2d->Unlock2D();
        }
        else if (SUCCEEDED(buffer->Lock(&start, &length, nullptr)))
        {
            const LONG stride = m_format.rgb32 ? static_cast<LONG>(m_format.width * 4) : static_cast<LONG>(m_format.width);
            hr = m_engine.Render(QpcNow(), start, stride, length);
            buffer->Unlock();
        }
        if (FAILED(hr) && (m_delivered % 300) == 0)
            SC_LOG_ERROR(L"MediaStream: render failed 0x%08X", hr);
    }

    sample->SetSampleTime(MFGetSystemTime());
    sample->SetSampleDuration(m_format.FrameDuration());
    if (token)
        sample->SetUnknown(MFSampleExtension_Token, token.get());

    std::lock_guard g(m_lock);
    if (m_state == MF_STREAM_STATE_RUNNING && m_queue)
    {
        m_queue->QueueEventParamUnk(MEMediaSample, GUID_NULL, S_OK, sample.get());
        ++m_delivered;
    }
    (void)tickTime;
}

void MediaStream::PollSettings()
{
    const Hns now = QpcNow();
    if (now - m_lastSettingsPoll < kHnsPerSecond / 2)
        return;
    m_lastSettingsPoll = now;

    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(SettingsPath().c_str(), GetFileExInfoStandard, &data))
        return;
    if (CompareFileTime(&data.ftLastWriteTime, &m_settingsStamp) == 0)
        return;
    const bool first = m_settingsStamp.dwLowDateTime == 0 && m_settingsStamp.dwHighDateTime == 0;
    m_settingsStamp = data.ftLastWriteTime;
    if (first)
        return; // already loaded at start

    m_settings = Settings::Load(SettingsPath());
    log::SetLevel(static_cast<log::Level>(m_settings.logLevel));
    m_engine.UpdateSettings(m_settings);
    SC_LOG_INFO(L"MediaStream: settings reloaded (mode %s, engine %s)", ToString(m_settings.mode),
                ToString(m_settings.engine));
}

void MediaStream::PublishStatus(bool streaming)
{
    const auto stats = m_engine.Stats();
    Status st;
    st.streaming = streaming;
    st.processId = GetCurrentProcessId();
    st.inputFps = stats.inputFps;
    st.outputFps = stats.outputFps;
    st.addedLatencyMs = stats.addedLatencyMs;
    st.gpuMs = stats.gpuMs;
    st.width = m_format.width;
    st.height = m_format.height;
    st.engine = stats.flowEngine;
    st.upscale = stats.upscale;
    st.filters = stats.filters;
    st.mode = ToString(m_settings.mode);
    st.camera = stats.camera;
    st.adapter = stats.adapter;
    st.message = stats.message;
    st.Save(StatusPath());
}
} // namespace smoothcam
