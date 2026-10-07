#include "Settings.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <sstream>

namespace smoothcam
{
namespace
{
std::wstring_view Trim(std::wstring_view s)
{
    while (!s.empty() && std::iswspace(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && std::iswspace(s.back()))
        s.remove_suffix(1);
    return s;
}

bool EqualsNoCase(std::wstring_view a, std::wstring_view b)
{
    return a.size() == b.size() &&
           CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(), static_cast<int>(b.size()), TRUE) ==
               CSTR_EQUAL;
}

uint32_t ToUInt(const KeyValues& kv, std::wstring_view key, uint32_t fallback)
{
    auto it = kv.find(key);
    if (it == kv.end())
        return fallback;
    try
    {
        return static_cast<uint32_t>(std::stoul(it->second));
    }
    catch (...)
    {
        return fallback;
    }
}

bool ToBool(const KeyValues& kv, std::wstring_view key, bool fallback)
{
    auto it = kv.find(key);
    if (it == kv.end())
        return fallback;
    const auto v = Trim(it->second);
    if (v == L"1" || EqualsNoCase(v, L"true") || EqualsNoCase(v, L"yes") || EqualsNoCase(v, L"on"))
        return true;
    if (v == L"0" || EqualsNoCase(v, L"false") || EqualsNoCase(v, L"no") || EqualsNoCase(v, L"off"))
        return false;
    return fallback;
}

std::wstring ToString(const KeyValues& kv, std::wstring_view key)
{
    auto it = kv.find(key);
    return it == kv.end() ? std::wstring{} : it->second;
}
} // namespace

std::string ToUtf8(std::wstring_view text)
{
    if (text.empty())
        return {};
    const int len = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                        nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(std::string_view text)
{
    if (text.empty())
        return {};
    const int len = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), len);
    return out;
}

KeyValues ParseKeyValues(std::string_view utf8)
{
    // Skip a UTF-8 BOM if an editor added one.
    if (utf8.size() >= 3 && static_cast<unsigned char>(utf8[0]) == 0xEF &&
        static_cast<unsigned char>(utf8[1]) == 0xBB && static_cast<unsigned char>(utf8[2]) == 0xBF)
        utf8.remove_prefix(3);

    KeyValues out;
    const std::wstring text = FromUtf8(utf8);
    std::wstring_view rest = text;
    while (!rest.empty())
    {
        const auto eol = rest.find(L'\n');
        auto line = Trim(rest.substr(0, eol));
        rest = eol == std::wstring_view::npos ? std::wstring_view{} : rest.substr(eol + 1);

        if (line.empty() || line.front() == L'#' || line.front() == L';' || line.front() == L'[')
            continue;
        const auto eq = line.find(L'=');
        if (eq == std::wstring_view::npos)
            continue;
        auto key = Trim(line.substr(0, eq));
        auto value = Trim(line.substr(eq + 1));
        if (!key.empty())
            out[std::wstring(key)] = std::wstring(value);
    }
    return out;
}

std::string FormatKeyValues(const KeyValues& values, std::string_view header)
{
    std::string out;
    if (!header.empty())
    {
        out += "# ";
        out += header;
        out += "\r\n";
    }
    for (const auto& [key, value] : values)
    {
        out += ToUtf8(key);
        out += '=';
        out += ToUtf8(value);
        out += "\r\n";
    }
    return out;
}

bool ReadKeyValueFile(const std::filesystem::path& path, KeyValues& out)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;
    std::ostringstream buffer;
    buffer << file.rdbuf();
    out = ParseKeyValues(buffer.str());
    return true;
}

bool WriteKeyValueFile(const std::filesystem::path& path, const KeyValues& values, std::string_view header)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    auto temp = path;
    temp += L".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;
        const auto text = FormatKeyValues(values, header);
        file.write(text.data(), static_cast<std::streamsize>(text.size()));
        if (!file)
            return false;
    }
    return MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

const wchar_t* ToString(Mode mode)
{
    switch (mode)
    {
    case Mode::Quality: return L"quality";
    case Mode::Balanced: return L"balanced";
    case Mode::LowLatency: return L"lowlatency";
    case Mode::Off: return L"off";
    }
    return L"quality";
}

const wchar_t* ToString(EngineChoice engine)
{
    switch (engine)
    {
    case EngineChoice::Auto: return L"auto";
    case EngineChoice::Nvidia: return L"nvidia";
    case EngineChoice::Gpu: return L"gpu";
    case EngineChoice::Basic: return L"basic";
    }
    return L"auto";
}

std::optional<Mode> ParseMode(std::wstring_view text)
{
    text = Trim(text);
    for (auto m : {Mode::Quality, Mode::Balanced, Mode::LowLatency, Mode::Off})
        if (EqualsNoCase(text, ToString(m)))
            return m;
    return std::nullopt;
}

std::optional<EngineChoice> ParseEngine(std::wstring_view text)
{
    text = Trim(text);
    for (auto e : {EngineChoice::Auto, EngineChoice::Nvidia, EngineChoice::Gpu, EngineChoice::Basic})
        if (EqualsNoCase(text, ToString(e)))
            return e;
    // "cpu" was the name used in early builds.
    if (EqualsNoCase(text, L"cpu"))
        return EngineChoice::Basic;
    return std::nullopt;
}

const wchar_t* ToString(Sharpening sharpening)
{
    switch (sharpening)
    {
    case Sharpening::Auto: return L"auto";
    case Sharpening::Off: return L"off";
    case Sharpening::Low: return L"low";
    case Sharpening::Medium: return L"medium";
    case Sharpening::High: return L"high";
    }
    return L"auto";
}

std::optional<Sharpening> ParseSharpening(std::wstring_view text)
{
    text = Trim(text);
    for (auto v : {Sharpening::Auto, Sharpening::Off, Sharpening::Low, Sharpening::Medium, Sharpening::High})
        if (EqualsNoCase(text, ToString(v)))
            return v;
    return std::nullopt;
}

namespace
{
template <class E, size_t N>
std::optional<E> ParseEnum(std::wstring_view text, const E (&values)[N])
{
    text = Trim(text);
    for (auto v : values)
        if (EqualsNoCase(text, ToString(v)))
            return v;
    return std::nullopt;
}
} // namespace

const wchar_t* ToString(NoiseReduction value)
{
    switch (value)
    {
    case NoiseReduction::Off: return L"off";
    case NoiseReduction::Low: return L"low";
    case NoiseReduction::Medium: return L"medium";
    case NoiseReduction::High: return L"high";
    }
    return L"off";
}

std::optional<NoiseReduction> ParseNoiseReduction(std::wstring_view text)
{
    static constexpr NoiseReduction values[] = {NoiseReduction::Off, NoiseReduction::Low, NoiseReduction::Medium,
                                                NoiseReduction::High};
    return ParseEnum(text, values);
}

const wchar_t* ToString(LowLightBoost value)
{
    switch (value)
    {
    case LowLightBoost::Off: return L"off";
    case LowLightBoost::Auto: return L"auto";
    case LowLightBoost::Strong: return L"strong";
    }
    return L"off";
}

std::optional<LowLightBoost> ParseLowLightBoost(std::wstring_view text)
{
    static constexpr LowLightBoost values[] = {LowLightBoost::Off, LowLightBoost::Auto, LowLightBoost::Strong};
    return ParseEnum(text, values);
}

const wchar_t* ToString(Background value)
{
    switch (value)
    {
    case Background::Off: return L"off";
    case Background::Blur: return L"blur";
    case Background::StrongBlur: return L"strong_blur";
    }
    return L"off";
}

std::optional<Background> ParseBackground(std::wstring_view text)
{
    static constexpr Background values[] = {Background::Off, Background::Blur, Background::StrongBlur};
    return ParseEnum(text, values);
}

const wchar_t* ToString(AutoFrame value)
{
    switch (value)
    {
    case AutoFrame::Off: return L"off";
    case AutoFrame::Wide: return L"wide";
    case AutoFrame::Close: return L"close";
    }
    return L"off";
}

std::optional<AutoFrame> ParseAutoFrame(std::wstring_view text)
{
    static constexpr AutoFrame values[] = {AutoFrame::Off, AutoFrame::Wide, AutoFrame::Close};
    return ParseEnum(text, values);
}

const wchar_t* ToString(Upscaler value)
{
    switch (value)
    {
    case Upscaler::Fsr: return L"fsr";
    case Upscaler::Nis: return L"nis";
    }
    return L"fsr";
}

std::optional<Upscaler> ParseUpscaler(std::wstring_view text)
{
    static constexpr Upscaler values[] = {Upscaler::Fsr, Upscaler::Nis};
    return ParseEnum(text, values);
}

uint32_t ClampOutputHeight(uint32_t height)
{
    // 0 means "the camera's own resolution"; otherwise snap to a supported size.
    if (height == 0)
        return 0;
    static constexpr uint32_t allowed[] = {480, 720, 1080, 1440, 2160};
    uint32_t closest = allowed[0];
    for (auto a : allowed)
        if ((a > height ? a - height : height - a) < (closest > height ? closest - height : height - closest))
            closest = a;
    return closest;
}

uint32_t CameraDefaultHeight(const Settings& settings)
{
    return settings.maxHeight ? ClampOutputHeight(settings.maxHeight) : 720;
}

uint32_t EffectiveOutputHeight(const Settings& settings)
{
    uint32_t w = 0, h = 0;
    EffectiveOutputSize(settings, w, h);
    return h;
}

void EffectiveOutputSize(const Settings& settings, uint32_t& width, uint32_t& height)
{
    if (settings.outputWidth && settings.outputHeight)
    {
        width = settings.outputWidth;
        height = settings.outputHeight;
    }
    else if (settings.maxWidth && settings.maxHeight)
    {
        width = settings.maxWidth & ~1u;
        height = settings.maxHeight & ~1u;
    }
    else
    {
        width = 1280;
        height = 720;
    }
}

uint32_t WidthForHeight(uint32_t height)
{
    return ((height * 16 / 9) + 1) & ~1u;
}

uint32_t ClampOutputFps(uint32_t fps)
{
    // Only rates that video apps commonly negotiate.
    static constexpr uint32_t allowed[] = {24, 25, 30, 48, 50, 60};
    uint32_t best = 60;
    uint32_t bestDiff = UINT32_MAX;
    for (auto a : allowed)
    {
        const uint32_t diff = a > fps ? a - fps : fps - a;
        if (diff < bestDiff)
        {
            best = a;
            bestDiff = diff;
        }
    }
    return best;
}

Settings Settings::Load(const std::filesystem::path& path)
{
    Settings s;
    KeyValues kv;
    if (!ReadKeyValueFile(path, kv))
        return s;

    s.device = ToString(kv, L"device");
    s.deviceName = ToString(kv, L"device_name");
    s.maxWidth = ToUInt(kv, L"max_width", 0);
    s.maxHeight = ToUInt(kv, L"max_height", 0);
    s.cameraFps = ToUInt(kv, L"camera_fps", 0);
    s.outputFps = ClampOutputFps(ToUInt(kv, L"output_fps", 60));
    // Even sizes up to 4K; anything else means "camera default".
    s.outputWidth = ToUInt(kv, L"output_width", 0);
    s.outputHeight = ToUInt(kv, L"output_height", 0);
    if (s.outputWidth < 160 || s.outputHeight < 120 || s.outputWidth > 3840 || s.outputHeight > 2160 ||
        (s.outputWidth | s.outputHeight) & 1u)
        s.outputWidth = s.outputHeight = 0;
    if (auto it = kv.find(L"sharpening"); it != kv.end())
        s.sharpening = ParseSharpening(it->second).value_or(Sharpening::Auto);
    if (auto it = kv.find(L"noise_reduction"); it != kv.end())
        s.noiseReduction = ParseNoiseReduction(it->second).value_or(NoiseReduction::Off);
    if (auto it = kv.find(L"low_light"); it != kv.end())
        s.lowLight = ParseLowLightBoost(it->second).value_or(LowLightBoost::Off);
    if (auto it = kv.find(L"background"); it != kv.end())
        s.background = ParseBackground(it->second).value_or(Background::Off);
    if (auto it = kv.find(L"auto_frame"); it != kv.end())
        s.autoFrame = ParseAutoFrame(it->second).value_or(AutoFrame::Off);
    if (auto it = kv.find(L"upscaler"); it != kv.end())
        s.upscaler = ParseUpscaler(it->second).value_or(Upscaler::Fsr);
    if (auto it = kv.find(L"mode"); it != kv.end())
        s.mode = ParseMode(it->second).value_or(Mode::Quality);
    if (auto it = kv.find(L"engine"); it != kv.end())
        s.engine = ParseEngine(it->second).value_or(EngineChoice::Auto);
    s.steadyFps = ToBool(kv, L"steady_fps", true);
    s.compare = ToBool(kv, L"compare", false);
    s.logLevel = static_cast<int>(std::min<uint32_t>(ToUInt(kv, L"log_level", 1), 2));
    return s;
}

bool Settings::Save(const std::filesystem::path& path) const
{
    KeyValues kv;
    kv[L"device"] = device;
    kv[L"device_name"] = deviceName;
    kv[L"max_width"] = std::to_wstring(maxWidth);
    kv[L"max_height"] = std::to_wstring(maxHeight);
    kv[L"camera_fps"] = std::to_wstring(cameraFps);
    kv[L"output_fps"] = std::to_wstring(outputFps);
    kv[L"output_width"] = std::to_wstring(outputWidth);
    kv[L"output_height"] = std::to_wstring(outputHeight);
    kv[L"sharpening"] = ToString(sharpening);
    kv[L"noise_reduction"] = ToString(noiseReduction);
    kv[L"low_light"] = ToString(lowLight);
    kv[L"background"] = ToString(background);
    kv[L"auto_frame"] = ToString(autoFrame);
    kv[L"upscaler"] = ToString(upscaler);
    kv[L"mode"] = ToString(mode);
    kv[L"engine"] = ToString(engine);
    kv[L"steady_fps"] = steadyFps ? L"1" : L"0";
    kv[L"compare"] = compare ? L"1" : L"0";
    kv[L"log_level"] = std::to_wstring(logLevel);
    return WriteKeyValueFile(path, kv, "SmoothCam settings - edited by SmoothCam.exe");
}
} // namespace smoothcam
