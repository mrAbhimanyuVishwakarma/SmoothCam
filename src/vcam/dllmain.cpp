#include "Activator.h"
#include "Guids.h"
#include "Log.h"

#include <windows.h>
#include <wrl/module.h>

#include <wil/registry.h>
#include <wil/result.h>

#include <string>

using namespace Microsoft::WRL;

namespace
{
HMODULE g_module = nullptr;

std::wstring ModulePath()
{
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD len = GetModuleFileNameW(g_module, path.data(), static_cast<DWORD>(path.size()));
        if (len < path.size())
        {
            path.resize(len);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

constexpr wchar_t kClsidKey[] = L"Software\\Classes\\CLSID\\{82EACDC9-EBE9-4362-9252-B22E63C3F106}";
} // namespace

using smoothcam::Activator;
CoCreatableClass(Activator);

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
        // Route WIL failure logging (RETURN_IF_FAILED etc.) into our log.
        wil::SetResultLoggingCallback([](const wil::FailureInfo& failure) noexcept {
            wchar_t text[1024];
            if (SUCCEEDED(wil::GetFailureLogString(text, ARRAYSIZE(text), failure)))
                smoothcam::log::Write(smoothcam::log::Level::Verbose, L"%s", text);
        });
    }
    return TRUE;
}

STDAPI DllCanUnloadNow()
{
    return Module<InProc>::GetModule().Terminate() ? S_OK : S_FALSE;
}

_Check_return_ STDAPI DllGetClassObject(_In_ REFCLSID clsid, _In_ REFIID riid, _Outptr_ LPVOID* ppv)
{
    return Module<InProc>::GetModule().GetClassObject(clsid, riid, ppv);
}

// Frame Server only loads virtual camera sources registered machine-wide, so
// this needs an elevated caller (the installer, or regsvr32 from an admin prompt).
STDAPI DllRegisterServer()
{
    wil::unique_hkey clsid;
    RETURN_IF_FAILED(wil::reg::create_unique_key_nothrow(HKEY_LOCAL_MACHINE, kClsidKey, clsid, wil::reg::key_access::readwrite));
    RETURN_IF_FAILED(wil::reg::set_value_string_nothrow(clsid.get(), nullptr, L"SmoothCam Media Source"));

    wil::unique_hkey server;
    RETURN_IF_FAILED(wil::reg::create_unique_key_nothrow(clsid.get(), L"InprocServer32", server, wil::reg::key_access::readwrite));
    RETURN_IF_FAILED(wil::reg::set_value_string_nothrow(server.get(), nullptr, ModulePath().c_str()));
    RETURN_IF_FAILED(wil::reg::set_value_string_nothrow(server.get(), L"ThreadingModel", L"Both"));
    return S_OK;
}

STDAPI DllUnregisterServer()
{
    const LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, kClsidKey);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(status);
}
