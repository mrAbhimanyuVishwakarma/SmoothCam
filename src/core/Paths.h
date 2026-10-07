#pragma once

#include <filesystem>

namespace smoothcam
{
// C:\ProgramData\SmoothCam - shared between the app (user) and the media
// source (LOCAL SERVICE inside Frame Server). The installer sets its ACL.
std::filesystem::path DataDirectory();

std::filesystem::path SettingsPath();
std::filesystem::path StatusPath();
std::filesystem::path LogDirectory();

// Folder that contains the calling module (exe or dll).
std::filesystem::path ModuleDirectory(void* moduleHandle = nullptr);
} // namespace smoothcam
