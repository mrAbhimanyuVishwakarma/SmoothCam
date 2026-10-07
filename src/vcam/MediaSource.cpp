#include "MediaSource.h"

#include "Log.h"
#include "MediaStream.h"
#include "Paths.h"
#include "Settings.h"

#include <ksmedia.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfvirtualcamera.h>
#include <propvarutil.h>

#include <wil/resource.h>
#include <wil/result.h>

namespace smoothcam
{
namespace
{
constexpr DWORD kStreamId = 0;
} // namespace

HRESULT MediaSource::RuntimeClassInitialize(IMFAttributes* activateAttributes)
{
    const auto settings = Settings::Load(SettingsPath());
    log::Init(LogDirectory(), L"vcam", static_cast<log::Level>(settings.logLevel));
    SC_LOG_INFO(L"MediaSource: created in process %lu", GetCurrentProcessId());

    RETURN_IF_FAILED(CreateSourceAttributes(activateAttributes));
    RETURN_IF_FAILED(MFCreateEventQueue(&m_queue));

    RETURN_IF_FAILED(Microsoft::WRL::MakeAndInitialize<MediaStream>(&m_stream, this, kStreamId, settings));
    wil::com_ptr_nothrow<IMFStreamDescriptor> sd;
    RETURN_IF_FAILED(m_stream->GetStreamDescriptor(&sd));
    IMFStreamDescriptor* descriptors[] = {sd.get()};
    RETURN_IF_FAILED(MFCreatePresentationDescriptor(1, descriptors, &m_descriptor));
    return S_OK;
}

HRESULT MediaSource::CreateSourceAttributes(IMFAttributes* activateAttributes)
{
    RETURN_IF_FAILED(MFCreateAttributes(&m_attributes, 4));
    if (activateAttributes)
        RETURN_IF_FAILED(activateAttributes->CopyAllItems(m_attributes.get()));

    // Sensor profiles. The legacy profile is mandatory so apps that don't know
    // about profiles still see every format; the high-frame-rate profile lets
    // profile-aware apps (Windows Camera) pick the 60 fps modes explicitly.
    wil::com_ptr_nothrow<IMFSensorProfileCollection> profiles;
    RETURN_IF_FAILED(MFCreateSensorProfileCollection(&profiles));
    wil::com_ptr_nothrow<IMFSensorProfile> profile;
    RETURN_IF_FAILED(MFCreateSensorProfile(KSCAMERAPROFILE_Legacy, 0, nullptr, &profile));
    RETURN_IF_FAILED(profile->AddProfileFilter(kStreamId, L"((RES==;FRT<=60,1;SUT==))"));
    RETURN_IF_FAILED(profiles->AddProfile(profile.get()));
    profile.reset();
    RETURN_IF_FAILED(MFCreateSensorProfile(KSCAMERAPROFILE_HighFrameRate, 0, nullptr, &profile));
    RETURN_IF_FAILED(profile->AddProfileFilter(kStreamId, L"((RES==;FRT>=48,1;SUT==))"));
    RETURN_IF_FAILED(profiles->AddProfile(profile.get()));
    RETURN_IF_FAILED(m_attributes->SetUnknown(MF_DEVICEMFT_SENSORPROFILE_COLLECTION, profiles.get()));
    return S_OK;
}

// ---- IMFMediaEventGenerator ------------------------------------------------

STDMETHODIMP MediaSource::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state)
{
    std::lock_guard g(m_lock);
    RETURN_IF_FAILED(CheckShutdown());
    return m_queue->BeginGetEvent(callback, state);
}

STDMETHODIMP MediaSource::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event)
{
    std::lock_guard g(m_lock);
    RETURN_IF_FAILED(CheckShutdown());
    return m_queue->EndGetEvent(result, event);
}

STDMETHODIMP MediaSource::GetEvent(DWORD flags, IMFMediaEvent** event)
{
    // GetEvent can block, so don't hold the lock while waiting.
    wil::com_ptr_nothrow<IMFMediaEventQueue> queue;
    {
        std::lock_guard g(m_lock);
        RETURN_IF_FAILED(CheckShutdown());
        queue = m_queue;
    }
    return queue->GetEvent(flags, event);
}

STDMETHODIMP MediaSource::QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* val)
{
    std::lock_guard g(m_lock);
    RETURN_IF_FAILED(CheckShutdown());
    return m_queue->QueueEventParamVar(type, extendedType, status, val);
}

// ---- IMFMediaSource --------------------------------------------------------

STDMETHODIMP MediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor)
{
    RETURN_HR_IF_NULL(E_POINTER, descriptor);
    *descriptor = nullptr;
    std::lock_guard g(m_lock);
    RETURN_IF_FAILED(CheckShutdown());
    return m_descriptor->Clone(descriptor);
}

STDMETHODIMP MediaSource::GetCharacteristics(DWORD* characteristics)
{
    RETURN_HR_IF_NULL(E_POINTER, characteristics);
    std::lock_guard g(m_lock);
    RETURN_IF_FAILED(CheckShutdown());
    *characteristics = MFMEDIASOURCE_IS_LIVE;
    return S_OK;
}

STDMETHODIMP MediaSource::Pause()
{
    return MF_E_INVALID_STATE_TRANSITION;
}

STDMETHODIMP MediaSource::Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat,
                                const PROPVARIANT* startPosition)
{
    RETURN_HR_IF_NULL(E_INVALIDARG, descriptor);
    RETURN_HR_IF_NULL(E_INVALIDARG, startPosition);
    RETURN_HR_IF(MF_E_UNSUPPORTED_TIME_FORMAT, timeFormat && *timeFormat != GUID_NULL);

    std::lock_guard g(m_lock);
    RETURN_IF_FAILED(CheckShutdown());

    DWORD count = 0;
    RETURN_IF_FAILED(descriptor->GetStreamDescriptorCount(&count));
    RETURN_HR_IF(E_INVALIDARG, count != 1);

    BOOL selected = FALSE;
    wil::com_ptr_nothrow<IMFStreamDescriptor> sd;
    RETURN_IF_FAILED(descriptor->GetStreamDescriptorByIndex(0, &selected, &sd));
    DWORD id = 0;
    RETURN_IF_FAILED(sd->GetStreamIdentifier(&id));
    RETURN_HR_IF(MF_E_INVALIDSTREAMNUMBER, id != kStreamId);

    wil::unique_prop_variant now;
    RETURN_IF_FAILED(InitPropVariantFromInt64(MFGetSystemTime(), &now));

    if (selected)
    {
        wil::com_ptr_nothrow<IMFMediaTypeHandler> handler;
        wil::com_ptr_nothrow<IMFMediaType> type;
        RETURN_IF_FAILED(sd->GetMediaTypeHandler(&handler));
        RETURN_IF_FAILED(handler->GetCurrentMediaType(&type));

        const bool wasActive = m_stream->IsActive();
        RETURN_IF_FAILED(m_descriptor->SelectStream(0));
        wil::com_ptr_nothrow<IUnknown> unk;
        RETURN_IF_FAILED(m_stream.CopyTo(IID_PPV_ARGS(&unk)));
        RETURN_IF_FAILED(m_queue->QueueEventParamUnk(wasActive ? MEUpdatedStream : MENewStream, GUID_NULL, S_OK,
                                                     unk.get()));
        RETURN_IF_FAILED(m_stream->Start(type.get()));
    }
    else
    {
        RETURN_IF_FAILED(m_descriptor->DeselectStream(0));
        RETURN_IF_FAILED(m_stream->Stop(true));
    }

    return m_queue->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &now);
}

STDMETHODIMP MediaSource::Stop()
{
    std::lock_guard g(m_lock);
    RETURN_IF_FAILED(CheckShutdown());
    RETURN_IF_FAILED(m_stream->Stop(true));
    RETURN_IF_FAILED(m_descriptor->DeselectStream(0));

    wil::unique_prop_variant now;
    RETURN_IF_FAILED(InitPropVariantFromInt64(MFGetSystemTime(), &now));
    return m_queue->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, &now);
}

STDMETHODIMP MediaSource::Shutdown()
{
    std::lock_guard g(m_lock);
    if (m_shutdown)
        return S_OK;
    m_shutdown = true;
    SC_LOG_INFO(L"MediaSource: shutdown");
    if (m_stream)
        m_stream->Shutdown();
    if (m_queue)
        m_queue->Shutdown();
    return S_OK;
}

// ---- IMFMediaSourceEx ------------------------------------------------------

STDMETHODIMP MediaSource::GetSourceAttributes(IMFAttributes** attributes)
{
    RETURN_HR_IF_NULL(E_POINTER, attributes);
    std::lock_guard g(m_lock);
    RETURN_IF_FAILED(CheckShutdown());
    return m_attributes.copy_to(attributes);
}

STDMETHODIMP MediaSource::GetStreamAttributes(DWORD streamId, IMFAttributes** attributes)
{
    RETURN_HR_IF_NULL(E_POINTER, attributes);
    std::lock_guard g(m_lock);
    RETURN_IF_FAILED(CheckShutdown());
    RETURN_HR_IF(MF_E_INVALIDSTREAMNUMBER, streamId != kStreamId);
    return m_stream->GetAttributes(attributes);
}

STDMETHODIMP MediaSource::SetD3DManager(IUnknown*)
{
    // We render with our own D3D11 device (on the high-performance GPU) and
    // deliver system-memory samples, so the pipeline's manager isn't needed.
    return S_OK;
}

// ---- IMFGetService ----------------------------------------------------------

STDMETHODIMP MediaSource::GetService(REFGUID, REFIID, LPVOID* object)
{
    if (object)
        *object = nullptr;
    return MF_E_UNSUPPORTED_SERVICE;
}

// ---- IKsControl ------------------------------------------------------------

STDMETHODIMP MediaSource::KsProperty(PKSPROPERTY, ULONG, LPVOID, ULONG, ULONG* bytesReturned)
{
    if (bytesReturned)
        *bytesReturned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

STDMETHODIMP MediaSource::KsMethod(PKSMETHOD, ULONG, LPVOID, ULONG, ULONG* bytesReturned)
{
    if (bytesReturned)
        *bytesReturned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

STDMETHODIMP MediaSource::KsEvent(PKSEVENT, ULONG, LPVOID, ULONG, ULONG* bytesReturned)
{
    if (bytesReturned)
        *bytesReturned = 0;
    return HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND);
}

// ---- IMFSampleAllocatorControl ----------------------------------------------

STDMETHODIMP MediaSource::SetDefaultAllocator(DWORD outputStreamId, IUnknown* allocator)
{
    RETURN_HR_IF_NULL(E_POINTER, allocator);
    std::lock_guard g(m_lock);
    RETURN_IF_FAILED(CheckShutdown());
    RETURN_HR_IF(MF_E_INVALIDSTREAMNUMBER, outputStreamId != kStreamId);
    return m_stream->SetAllocator(allocator);
}

STDMETHODIMP MediaSource::GetAllocatorUsage(DWORD outputStreamId, DWORD* inputStreamId, MFSampleAllocatorUsage* usage)
{
    RETURN_HR_IF_NULL(E_POINTER, inputStreamId);
    RETURN_HR_IF_NULL(E_POINTER, usage);
    RETURN_HR_IF(MF_E_INVALIDSTREAMNUMBER, outputStreamId != kStreamId);
    *inputStreamId = outputStreamId;
    *usage = MFSampleAllocatorUsage_UsesProvidedAllocator;
    return S_OK;
}
} // namespace smoothcam
