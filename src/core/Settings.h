#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace smoothcam
{
enum class Mode
{
    Quality,    // wait for the next real frame, best motion estimation
    Balanced,   // same, cheaper motion estimation
    LowLatency, // extrapolate forward, no waiting
    Off,        // passthrough
};

enum class EngineChoice
{
    Auto,
    Nvidia, // NVIDIA Optical Flow hardware
    Gpu,    // SmoothCam's own compute-shader flow, any GPU
    Basic,  // no motion estimation, plain blend
};

// RCAS sharpening strength. Auto = only when upscaling.
enum class Sharpening
{
    Auto,
    Off,
    Low,
    Medium,
    High,
};

// GPU noise / grain reduction (motion-compensated temporal + light spatial).
enum class NoiseReduction
{
    Off,
    Low,
    Medium,
    High,
};

// Software brightening for dim rooms. Unlike the camera's own low-light mode it
// doesn't lower the camera's frame rate.
enum class LowLightBoost
{
    Off,
    Auto,
    Strong,
};

// Background effect, using on-device person segmentation.
enum class Background
{
    Off,
    Blur,
    StrongBlur,
};

// Automatic framing: follow the face and zoom in on it.
enum class AutoFrame
{
    Off,
    Wide,  // gentle: head and shoulders
    Close, // tighter: face fills more of the frame
};

// Which spatial upscaler takes the picture past the camera's resolution.
enum class Upscaler
{
    Fsr, // AMD FidelityFX Super Resolution 1 (any ratio)
    Nis, // NVIDIA Image Scaling (up to 2x; bigger steps fall back to FSR)
};

struct Settings
{
    std::wstring device;     // symbolic link of the physical camera; empty = first camera found
    std::wstring deviceName; // friendly name, for display only
    uint32_t maxWidth = 0;   // camera's default (largest full-rate) resolution, 0 = unknown
    uint32_t maxHeight = 0;
    uint32_t cameraFps = 0;  // camera's default frame rate, 0 = unknown
    uint32_t outputFps = 60; // what apps receive: 24 ... 60
    // Output resolution. 0 x 0 = the camera's own; anything larger than the
    // camera delivers is reached with FSR 1 upscaling (up to 3840 x 2160).
    uint32_t outputWidth = 0;
    uint32_t outputHeight = 0;
    Sharpening sharpening = Sharpening::Auto;
    NoiseReduction noiseReduction = NoiseReduction::Off;
    LowLightBoost lowLight = LowLightBoost::Off;
    Background background = Background::Off;
    AutoFrame autoFrame = AutoFrame::Off;
    Upscaler upscaler = Upscaler::Fsr;
    Mode mode = Mode::Quality;
    EngineChoice engine = EngineChoice::Auto;
    bool steadyFps = true;   // ask the camera to keep its frame rate in low light
    bool compare = false;    // split screen: original left, SmoothCam right
    int logLevel = 1;        // 0 errors, 1 info, 2 verbose

    bool operator==(const Settings&) const = default;

    static Settings Load(const std::filesystem::path& path);
    bool Save(const std::filesystem::path& path) const;
};

const wchar_t* ToString(Mode mode);
const wchar_t* ToString(EngineChoice engine);
const wchar_t* ToString(Sharpening sharpening);
std::optional<Sharpening> ParseSharpening(std::wstring_view text);
const wchar_t* ToString(NoiseReduction value);
std::optional<NoiseReduction> ParseNoiseReduction(std::wstring_view text);
const wchar_t* ToString(LowLightBoost value);
std::optional<LowLightBoost> ParseLowLightBoost(std::wstring_view text);
const wchar_t* ToString(Background value);
std::optional<Background> ParseBackground(std::wstring_view text);
const wchar_t* ToString(AutoFrame value);
std::optional<AutoFrame> ParseAutoFrame(std::wstring_view text);
const wchar_t* ToString(Upscaler value);
std::optional<Upscaler> ParseUpscaler(std::wstring_view text);
uint32_t ClampOutputHeight(uint32_t height);

// The camera's own resolution snapped to the nearest standard size (720 if unknown).
uint32_t CameraDefaultHeight(const Settings& settings);
// The resolution SmoothCam actually delivers: the user's choice, or the camera default.
uint32_t EffectiveOutputHeight(const Settings& settings);
void EffectiveOutputSize(const Settings& settings, uint32_t& width, uint32_t& height);
// 16:9 width for a standard height (480 -> 854, 720 -> 1280, ...), always even.
uint32_t WidthForHeight(uint32_t height);
std::optional<Mode> ParseMode(std::wstring_view text);
std::optional<EngineChoice> ParseEngine(std::wstring_view text);
uint32_t ClampOutputFps(uint32_t fps);

// Tiny UTF-8 "key=value" file format shared by settings.ini and status.ini.
// Lines starting with '#' or ';' and [sections] are ignored.
using KeyValues = std::map<std::wstring, std::wstring, std::less<>>;
KeyValues ParseKeyValues(std::string_view utf8);
std::string FormatKeyValues(const KeyValues& values, std::string_view header = {});
bool ReadKeyValueFile(const std::filesystem::path& path, KeyValues& out);
// Writes to a temp file and renames over the target, so readers never see a half-written file.
bool WriteKeyValueFile(const std::filesystem::path& path, const KeyValues& values, std::string_view header = {});

std::string ToUtf8(std::wstring_view text);
std::wstring FromUtf8(std::string_view text);
} // namespace smoothcam
