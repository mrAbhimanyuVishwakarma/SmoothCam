#include "Log.h"

#include <windows.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace smoothcam::log
{
namespace
{
constexpr uintmax_t kMaxFileSize = 1024 * 1024;
constexpr int kKeepFiles = 3;

struct State
{
    std::mutex lock;
    std::filesystem::path dir;
    std::wstring name;
    FILE* file = nullptr;
    Level level = Level::Info;
    bool initialized = false;
};

State& GetState()
{
    static State* state = new State(); // leaked on purpose: usable during DLL unload
    return *state;
}

std::filesystem::path FileName(const State& s, int index)
{
    return s.dir / (index == 0 ? s.name + L".log" : s.name + L"." + std::to_wstring(index) + L".log");
}

void OpenLocked(State& s)
{
    if (s.file)
        return;
    std::error_code ec;
    std::filesystem::create_directories(s.dir, ec);
    s.file = _wfsopen(FileName(s, 0).c_str(), L"ab", _SH_DENYNO);
}

void RotateLocked(State& s)
{
    if (!s.file || _ftelli64(s.file) < static_cast<long long>(kMaxFileSize))
        return;
    fclose(s.file);
    s.file = nullptr;
    std::error_code ec;
    std::filesystem::remove(FileName(s, kKeepFiles - 1), ec);
    for (int i = kKeepFiles - 2; i >= 0; --i)
        std::filesystem::rename(FileName(s, i), FileName(s, i + 1), ec);
    OpenLocked(s);
}
} // namespace

void Init(const std::filesystem::path& dir, std::wstring_view name, Level level)
{
    auto& s = GetState();
    std::lock_guard guard(s.lock);
    s.level = level;
    if (s.initialized)
        return;
    s.dir = dir;
    s.name = name;
    s.initialized = true;
    OpenLocked(s);
}

void SetLevel(Level level)
{
    auto& s = GetState();
    std::lock_guard guard(s.lock);
    s.level = level;
}

void Shutdown()
{
    auto& s = GetState();
    std::lock_guard guard(s.lock);
    if (s.file)
    {
        fclose(s.file);
        s.file = nullptr;
    }
    s.initialized = false;
}

void Write(Level level, const wchar_t* format, ...)
{
    auto& s = GetState();
    if (level > s.level)
        return;

    wchar_t message[1024];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, _TRUNCATE, format, args);
    va_end(args);

    SYSTEMTIME t;
    GetLocalTime(&t);
    static constexpr const wchar_t* tags[] = {L"ERR", L"INF", L"VRB"};
    wchar_t line[1200];
    swprintf_s(line, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [%5lu] %s %s\r\n", t.wYear, t.wMonth, t.wDay, t.wHour,
               t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId(), tags[static_cast<int>(level)], message);

    OutputDebugStringW(line);

    std::lock_guard guard(s.lock);
    if (!s.file)
        return;
    // Logs are UTF-8 so they open cleanly anywhere.
    char utf8[2400];
    const int len = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (len > 1)
    {
        fwrite(utf8, 1, static_cast<size_t>(len - 1), s.file);
        fflush(s.file);
    }
    RotateLocked(s);
}
} // namespace smoothcam::log
