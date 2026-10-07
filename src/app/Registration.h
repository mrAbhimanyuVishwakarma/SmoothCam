#pragma once

#include <windows.h>

#include <string>

namespace smoothcam
{
struct RegistrationState
{
    bool comRegistered = false; // HKLM CLSID entry pointing at an existing DLL
    std::wstring dllPath;
    bool cameraPresent = false; // "SmoothCam" shows up as a capture device
    std::wstring cameraName;
};

RegistrationState QueryRegistration();

// Both require an elevated process.
HRESULT RegisterCamera();   // COM server + system-wide virtual camera
HRESULT UnregisterCamera(); // the reverse

bool IsElevated();

// Runs this executable again elevated with `arguments` and waits for it.
// Returns its exit code, or an HRESULT if the user declined the UAC prompt.
DWORD RunElevated(const std::wstring& arguments);

// Creates C:\ProgramData\SmoothCam with the ACL the virtual camera needs
// (Users modify, LOCAL SERVICE modify). Elevated only.
HRESULT PrepareDataDirectory();
} // namespace smoothcam
