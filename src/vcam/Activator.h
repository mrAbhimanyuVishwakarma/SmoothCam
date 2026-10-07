#pragma once

#include "Guids.h"

#include <windows.h>
#include <mfidl.h>
#include <mfobjects.h>
#include <wrl/implements.h>

#include <wil/com.h>

namespace smoothcam
{
class MediaSource;

// The COM class Frame Server instantiates (CLSID_SmoothCamSource). Frame Server
// stores configuration on it as attributes, then calls ActivateObject to get
// the actual media source.
class DECLSPEC_UUID("82EACDC9-EBE9-4362-9252-B22E63C3F106") Activator
    : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
                                          Microsoft::WRL::ChainInterfaces<IMFActivate, IMFAttributes>>
{
public:
    Activator();

    // IMFActivate
    STDMETHODIMP ActivateObject(REFIID riid, void** ppv) override;
    STDMETHODIMP ShutdownObject() override;
    STDMETHODIMP DetachObject() override;

    // IMFAttributes - forwarded to an internal store.
    STDMETHODIMP GetItem(REFGUID key, PROPVARIANT* val) override { return m_attrs->GetItem(key, val); }
    STDMETHODIMP GetItemType(REFGUID key, MF_ATTRIBUTE_TYPE* type) override { return m_attrs->GetItemType(key, type); }
    STDMETHODIMP CompareItem(REFGUID key, REFPROPVARIANT val, BOOL* result) override
    {
        return m_attrs->CompareItem(key, val, result);
    }
    STDMETHODIMP Compare(IMFAttributes* theirs, MF_ATTRIBUTES_MATCH_TYPE type, BOOL* result) override
    {
        return m_attrs->Compare(theirs, type, result);
    }
    STDMETHODIMP GetUINT32(REFGUID key, UINT32* val) override { return m_attrs->GetUINT32(key, val); }
    STDMETHODIMP GetUINT64(REFGUID key, UINT64* val) override { return m_attrs->GetUINT64(key, val); }
    STDMETHODIMP GetDouble(REFGUID key, double* val) override { return m_attrs->GetDouble(key, val); }
    STDMETHODIMP GetGUID(REFGUID key, GUID* val) override { return m_attrs->GetGUID(key, val); }
    STDMETHODIMP GetStringLength(REFGUID key, UINT32* length) override { return m_attrs->GetStringLength(key, length); }
    STDMETHODIMP GetString(REFGUID key, LPWSTR val, UINT32 size, UINT32* length) override
    {
        return m_attrs->GetString(key, val, size, length);
    }
    STDMETHODIMP GetAllocatedString(REFGUID key, LPWSTR* val, UINT32* length) override
    {
        return m_attrs->GetAllocatedString(key, val, length);
    }
    STDMETHODIMP GetBlobSize(REFGUID key, UINT32* size) override { return m_attrs->GetBlobSize(key, size); }
    STDMETHODIMP GetBlob(REFGUID key, UINT8* buf, UINT32 size, UINT32* blobSize) override
    {
        return m_attrs->GetBlob(key, buf, size, blobSize);
    }
    STDMETHODIMP GetAllocatedBlob(REFGUID key, UINT8** buf, UINT32* size) override
    {
        return m_attrs->GetAllocatedBlob(key, buf, size);
    }
    STDMETHODIMP GetUnknown(REFGUID key, REFIID riid, LPVOID* ppv) override { return m_attrs->GetUnknown(key, riid, ppv); }
    STDMETHODIMP SetItem(REFGUID key, REFPROPVARIANT val) override { return m_attrs->SetItem(key, val); }
    STDMETHODIMP DeleteItem(REFGUID key) override { return m_attrs->DeleteItem(key); }
    STDMETHODIMP DeleteAllItems() override { return m_attrs->DeleteAllItems(); }
    STDMETHODIMP SetUINT32(REFGUID key, UINT32 val) override { return m_attrs->SetUINT32(key, val); }
    STDMETHODIMP SetUINT64(REFGUID key, UINT64 val) override { return m_attrs->SetUINT64(key, val); }
    STDMETHODIMP SetDouble(REFGUID key, double val) override { return m_attrs->SetDouble(key, val); }
    STDMETHODIMP SetGUID(REFGUID key, REFGUID val) override { return m_attrs->SetGUID(key, val); }
    STDMETHODIMP SetString(REFGUID key, LPCWSTR val) override { return m_attrs->SetString(key, val); }
    STDMETHODIMP SetBlob(REFGUID key, const UINT8* buf, UINT32 size) override { return m_attrs->SetBlob(key, buf, size); }
    STDMETHODIMP SetUnknown(REFGUID key, IUnknown* val) override { return m_attrs->SetUnknown(key, val); }
    STDMETHODIMP LockStore() override { return m_attrs->LockStore(); }
    STDMETHODIMP UnlockStore() override { return m_attrs->UnlockStore(); }
    STDMETHODIMP GetCount(UINT32* count) override { return m_attrs->GetCount(count); }
    STDMETHODIMP GetItemByIndex(UINT32 index, GUID* key, PROPVARIANT* val) override
    {
        return m_attrs->GetItemByIndex(index, key, val);
    }
    STDMETHODIMP CopyAllItems(IMFAttributes* dest) override { return m_attrs->CopyAllItems(dest); }

private:
    wil::com_ptr_nothrow<IMFAttributes> m_attrs;
    Microsoft::WRL::ComPtr<MediaSource> m_source;
};
} // namespace smoothcam
