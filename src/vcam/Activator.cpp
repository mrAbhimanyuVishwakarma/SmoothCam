#include "Activator.h"

#include "Log.h"
#include "MediaSource.h"
#include "MediaStream.h"

#include <mfapi.h>
#include <mftransform.h>

#include <wil/result.h>

namespace smoothcam
{
Activator::Activator()
{
    // Can't fail a constructor; an allocation failure here surfaces as
    // E_POINTER-style failures on first use, which Frame Server handles.
    if (SUCCEEDED(MFCreateAttributes(&m_attrs, 4)))
        m_attrs->SetGUID(MFT_TRANSFORM_CLSID_Attribute, CLSID_SmoothCamSource);
}

STDMETHODIMP Activator::ActivateObject(REFIID riid, void** ppv)
{
    RETURN_HR_IF_NULL(E_POINTER, ppv);
    *ppv = nullptr;
    RETURN_HR_IF_NULL(E_OUTOFMEMORY, m_attrs);

    if (!m_source)
    {
        Microsoft::WRL::ComPtr<MediaSource> source;
        RETURN_IF_FAILED(Microsoft::WRL::MakeAndInitialize<MediaSource>(&source, m_attrs.get()));
        m_source = source;
    }
    return m_source->QueryInterface(riid, ppv);
}

STDMETHODIMP Activator::ShutdownObject()
{
    if (m_source)
        m_source->Shutdown();
    return S_OK;
}

STDMETHODIMP Activator::DetachObject()
{
    m_source = nullptr;
    return S_OK;
}
} // namespace smoothcam
