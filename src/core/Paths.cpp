#include "Paths.h"

#include <windows.h>
#include <shlobj.h>

#include <wil/resource.h>
#include <wil/win32_helpers.h>

namespace smoothcam
{
std::filesystem::path DataDirectory()
{
    wil::unique_cotaskmem_string programData;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramData, KF_FLAG_DEFAULT, nullptr, &programData)))
        return L"C:\\ProgramData\\SmoothCam";
    return std::filesystem::path(programData.get()) / L"SmoothCam";
}

std::filesystem::path SettingsPath()
{
    return DataDirectory() / L"settings.ini";
}

std::filesystem::path StatusPath()
{
    return DataDirectory() / L"status.ini";
}

std::filesystem::path LogDirectory()
{
    return DataDirectory() / L"logs";
}

std::filesystem::path ModuleDirectory(void* moduleHandle)
{
    HMODULE module = static_cast<HMODULE>(moduleHandle);
    if (!module)
    {
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ModuleDirectory), &module);
    }
    std::wstring path(MAX_PATH, L'\0');
    for (;;)
    {
        const DWORD len = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
        if (len < path.size())
        {
            path.resize(len);
            break;
        }
        path.resize(path.size() * 2);
    }
    return std::filesystem::path(path).parent_path();
}
} // namespace smoothcam
