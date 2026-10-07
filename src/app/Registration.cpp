#include "Registration.h"

#include "Guids.h"
#include "Log.h"
#include "Paths.h"
#include "capture/Camera.h"

#include <aclapi.h>
#include <mfapi.h>
#include <mfvirtualcamera.h>
#include <sddl.h>
#include <shellapi.h>

#include <wil/com.h>
#include <wil/registry.h>
#include <wil/resource.h>
#include <wil/result.h>

namespace smoothcam
{
namespace
{
constexpr wchar_t kServerKey[] =
    L"Software\\Classes\\CLSID\\{82EACDC9-EBE9-4362-9252-B22E63C3F106}\\InprocServer32";

std::filesystem::path SourceDllPath()
{
    return ModuleDirectory() / L"SmoothCamSource.dll";
}

HRESULT CallDllEntry(const char* name)
{
    const auto path = SourceDllPath();
    wil::unique_hmodule dll(LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH));
    RETURN_LAST_ERROR_IF_NULL(dll);
    auto fn = reinterpret_cast<HRESULT(STDAPICALLTYPE*)()>(GetProcAddress(dll.get(), name));
    RETURN_LAST_ERROR_IF_NULL(fn);
    return fn();
}

HRESULT CreateVirtualCamera(wil::com_ptr_nothrow<IMFVirtualCamera>& camera)
{
    // Same parameters every time: that's how Windows identifies "our" camera.
    return MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_System,
                                 MFVirtualCameraAccess_AllUsers, kCameraName, kClsidString, nullptr, 0, &camera);
}
} // namespace

RegistrationState QueryRegistration()
{
    RegistrationState state;
    wchar_t path[MAX_PATH * 2] = L"";
    DWORD size = sizeof(path);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, kServerKey, nullptr, RRF_RT_REG_SZ, nullptr, path, &size) == ERROR_SUCCESS)
    {
        state.dllPath = path;
        state.comRegistered = GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
    }
    for (const auto& cam : EnumerateAllCameras())
    {
        if (IsSmoothCamDevice(cam.name, {}))
        {
            state.cameraPresent = true;
            state.cameraName = cam.name;
            break;
        }
    }
    return state;
}

HRESULT PrepareDataDirectory()
{
    const auto dir = DataDirectory();
    std::error_code ec;
    std::filesystem::create_directories(dir / L"logs", ec);

    // SYSTEM + Administrators: full. Users and LOCAL SERVICE (Frame Server): modify.
    wil::unique_hlocal_security_descriptor sd;
    RETURN_IF_WIN32_BOOL_FALSE(ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)(A;OICI;0x1301bf;;;BU)(A;OICI;0x1301bf;;;LS)", SDDL_REVISION_1, &sd,
        nullptr));
    BOOL present = FALSE;
    BOOL defaulted = FALSE;
    PACL dacl = nullptr;
    RETURN_IF_WIN32_BOOL_FALSE(GetSecurityDescriptorDacl(sd.get(), &present, &dacl, &defaulted));
    std::wstring path = dir.wstring();
    RETURN_IF_WIN32_ERROR(SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT,
                                                DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                                nullptr, nullptr, dacl, nullptr));
    return S_OK;
}

HRESULT RegisterCamera()
{
    RETURN_IF_FAILED(PrepareDataDirectory());
    RETURN_IF_FAILED_MSG(CallDllEntry("DllRegisterServer"), "registering SmoothCamSource.dll");

    RETURN_IF_FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    auto shutdown = wil::scope_exit([] { MFShutdown(); });
    wil::com_ptr_nothrow<IMFVirtualCamera> camera;
    RETURN_IF_FAILED_MSG(CreateVirtualCamera(camera), "MFCreateVirtualCamera");
    RETURN_IF_FAILED_MSG(camera->Start(nullptr), "IMFVirtualCamera::Start");
    SC_LOG_INFO(L"Registration: virtual camera registered");
    return S_OK;
}

HRESULT UnregisterCamera()
{
    HRESULT result = S_OK;
    if (SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE)))
    {
        wil::com_ptr_nothrow<IMFVirtualCamera> camera;
        HRESULT hr = CreateVirtualCamera(camera);
        if (SUCCEEDED(hr))
            hr = camera->Remove();
        if (FAILED(hr))
        {
            SC_LOG_ERROR(L"Registration: removing the virtual camera failed (0x%08X)", hr);
            result = hr;
        }
        camera.reset();
        MFShutdown();
    }
    const HRESULT com = CallDllEntry("DllUnregisterServer");
    if (FAILED(com))
    {
        // Fall back to deleting the key ourselves if the DLL is already gone.
        RegDeleteTreeW(HKEY_LOCAL_MACHINE, L"Software\\Classes\\CLSID\\{82EACDC9-EBE9-4362-9252-B22E63C3F106}");
    }
    return result;
}

bool IsElevated()
{
    wil::unique_handle token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    return GetTokenInformation(token.get(), TokenElevation, &elevation, sizeof(elevation), &size) &&
           elevation.TokenIsElevated != 0;
}

DWORD RunElevated(const std::wstring& arguments)
{
    const auto exe = ModuleDirectory() / L"SmoothCam.exe";
    SHELLEXECUTEINFOW info{sizeof(info)};
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    info.lpVerb = L"runas";
    info.lpFile = exe.c_str();
    info.lpParameters = arguments.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info))
        return HRESULT_FROM_WIN32(GetLastError());
    wil::unique_handle process(info.hProcess);
    WaitForSingleObject(process.get(), INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.get(), &code);
    return code;
}
} // namespace smoothcam
