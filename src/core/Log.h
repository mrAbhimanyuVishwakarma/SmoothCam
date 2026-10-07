#pragma once

#include <filesystem>
#include <string_view>

namespace smoothcam::log
{
enum class Level
{
    Error = 0,
    Info = 1,
    Verbose = 2,
};

// Opens <dir>/<name>.log, rolling at ~1 MB and keeping 3 files. Safe to call
// more than once; later calls only change the level. If the directory isn't
// writable, logging silently goes to OutputDebugString only.
void Init(const std::filesystem::path& dir, std::wstring_view name, Level level);
void SetLevel(Level level);
void Shutdown();

void Write(Level level, const wchar_t* format, ...);
} // namespace smoothcam::log

#define SC_LOG_ERROR(...) ::smoothcam::log::Write(::smoothcam::log::Level::Error, __VA_ARGS__)
#define SC_LOG_INFO(...) ::smoothcam::log::Write(::smoothcam::log::Level::Info, __VA_ARGS__)
#define SC_LOG_VERBOSE(...) ::smoothcam::log::Write(::smoothcam::log::Level::Verbose, __VA_ARGS__)
