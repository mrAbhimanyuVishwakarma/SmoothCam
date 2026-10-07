// SmoothCam.exe
//
//   SmoothCam.exe                  settings window
//   SmoothCam.exe --register       install the virtual camera (asks for admin)
//   SmoothCam.exe --unregister     remove it (asks for admin)
//   SmoothCam.exe --diag           print GPUs, cameras and registration state
//   SmoothCam.exe --probe [secs] [--width W] [--nv12] [--snap DIR]
//                                  open the installed camera like an app would and measure it

#include "Diagnostics.h"
#include "Log.h"
#include "MainWindow.h"
#include "Paths.h"
#include "Registration.h"
#include "Settings.h"

#include <windows.h>
#include <commctrl.h>
#include <mfapi.h>
#include <shellapi.h>

#include <wil/resource.h>
#include <wil/result.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace smoothcam;

namespace
{
int Register(bool unregister)
{
    if (!IsElevated())
        return static_cast<int>(RunElevated(unregister ? L"--unregister" : L"--register"));
    const HRESULT hr = unregister ? UnregisterCamera() : RegisterCamera();
    if (FAILED(hr))
        Print(L"%s failed: 0x%08X\n", unregister ? L"Unregister" : L"Register", hr);
    else
        Print(L"%s\n", unregister ? L"SmoothCam camera removed." : L"SmoothCam camera registered.");
    return FAILED(hr) ? hr : 0;
}

int RunCommandLine(const std::vector<std::wstring>& args)
{
    const auto co = wil::CoInitializeEx(COINIT_MULTITHREADED);
    const std::wstring& verb = args[0];
    if (verb == L"--register")
        return Register(false);
    if (verb == L"--unregister")
        return Register(true);
    if (verb == L"--diag")
        return RunDiagnostics();
    if (verb == L"--probe")
    {
        int seconds = 8;
        uint32_t width = 1280;
        std::filesystem::path snap;
        bool nv12 = false;
        for (size_t i = 1; i < args.size(); ++i)
        {
            if (args[i] == L"--snap" && i + 1 < args.size())
                snap = args[++i];
            else if (args[i] == L"--width" && i + 1 < args.size())
                width = static_cast<uint32_t>(_wtoi(args[++i].c_str())); // 0 = app default
            else if (args[i] == L"--nv12")
                nv12 = true; // the format most apps take
            else
                seconds = std::max(2, _wtoi(args[i].c_str()));
        }
        return RunProbe(seconds, snap, width, nv12);
    }
    Print(L"Unknown option %s\nUsage: SmoothCam.exe [--register | --unregister | --diag | --probe [seconds] [--width W] [--nv12] [--snap DIR]]\n",
          verb.c_str());
    return 2;
}
} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);

    const auto settings = Settings::Load(SettingsPath());
    log::Init(LogDirectory(), L"app", static_cast<log::Level>(settings.logLevel));
    // Route WIL's failure reports (RETURN_IF_FAILED and friends) into our log.
    wil::SetResultLoggingCallback([](const wil::FailureInfo& failure) noexcept {
        wchar_t text[1024];
        if (SUCCEEDED(wil::GetFailureLogString(text, ARRAYSIZE(text), failure)))
            log::Write(log::Level::Error, L"%s", text);
    });

    int argc = 0;
    wil::unique_hlocal_ptr<LPWSTR[]> argv(CommandLineToArgvW(GetCommandLineW(), &argc));
    if (argc > 1)
    {
        std::vector<std::wstring> args(argv.get() + 1, argv.get() + argc);
        return RunCommandLine(args);
    }

    // One settings window at a time.
    wil::unique_mutex_nothrow single;
    single.reset(CreateMutexW(nullptr, TRUE, L"Local\\SmoothCam.SettingsWindow"));
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        if (HWND existing = FindWindowW(L"#32770", L"SmoothCam"))
        {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        return 0;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
    const auto co = wil::CoInitializeEx(COINIT_APARTMENTTHREADED);
    MFStartup(MF_VERSION, MFSTARTUP_LITE);
    const int result = MainWindow::Run(instance);
    MFShutdown();
    return result;
}
