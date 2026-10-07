#pragma once

#include "Engine.h"
#include "Settings.h"

#include <windows.h>
#include <mfidl.h>
#include <wrl/implements.h>

#include <wil/com.h>
#include <wil/resource.h>

#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>

namespace smoothcam
{
class MediaSource;

// The single video stream. Owns the Engine and an output clock thread that
// answers Frame Server's sample requests at a steady frame rate.
class MediaStream
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          Microsoft::WRL::ChainInterfaces<IMFMediaStream2, IMFMediaStream,
                                                                          IMFMediaEventGenerator>>
{
public:
    ~MediaStream() override;
    HRESULT RuntimeClassInitialize(IMFMediaSource* source, DWORD streamId, const Settings& settings);

    // IMFMediaEventGenerator
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
    STDMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
    STDMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
    STDMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* val) override;

    // IMFMediaStream
    STDMETHODIMP GetMediaSource(IMFMediaSource** source) override;
    STDMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** descriptor) override;
    STDMETHODIMP RequestSample(IUnknown* token) override;

    // IMFMediaStream2
    STDMETHODIMP SetStreamState(MF_STREAM_STATE state) override;
    STDMETHODIMP GetStreamState(MF_STREAM_STATE* state) override;

    // Called by MediaSource (with its lock held).
    HRESULT Start(IMFMediaType* type);
    HRESULT Stop(bool sendEvent);
    void Shutdown();
    HRESULT SetAllocator(IUnknown* allocator);
    HRESULT GetAttributes(IMFAttributes** attributes);
    bool IsActive();

private:
    HRESULT BuildMediaTypes(const Settings& settings);
    void ClockLoop();
    void DeliverFrame(Hns tickTime);
    void PollSettings();
    void PublishStatus(bool streaming);

    std::mutex m_lock;
    bool m_shutdown = false;
    MF_STREAM_STATE m_state = MF_STREAM_STATE_STOPPED;
    DWORD m_id = 0;
    IMFMediaSource* m_source = nullptr; // weak: the source owns us
    wil::com_ptr_nothrow<IMFMediaEventQueue> m_queue;
    wil::com_ptr_nothrow<IMFStreamDescriptor> m_descriptor;
    wil::com_ptr_nothrow<IMFAttributes> m_attributes;
    wil::com_ptr_nothrow<IMFVideoSampleAllocatorEx> m_allocator;
    wil::com_ptr_nothrow<IMFMediaType> m_type;
    std::deque<wil::com_ptr_nothrow<IUnknown>> m_requests;
    uint32_t m_pendingNullTokens = 0;

    // Output clock.
    std::thread m_clock;
    wil::unique_event m_stopClock{wil::EventOptions::ManualReset};
    OutputFormat m_format;

    // Engine and settings live on the clock thread.
    Engine m_engine;
    Settings m_settings;
    FILETIME m_settingsStamp{};
    Hns m_lastSettingsPoll = 0;
    Hns m_lastStatus = 0;
    uint64_t m_delivered = 0;
};
} // namespace smoothcam
