#pragma once

#include <guiddef.h>

namespace smoothcam
{
// COM class of the media source that Frame Server loads for the virtual camera.
// Never change this after release: the installed camera is keyed off it.
// {82EACDC9-EBE9-4362-9252-B22E63C3F106}
inline constexpr GUID CLSID_SmoothCamSource = {
    0x82eacdc9, 0xebe9, 0x4362, {0x92, 0x52, 0xb2, 0x2e, 0x63, 0xc3, 0xf1, 0x06}};

inline constexpr wchar_t kClsidString[] = L"{82EACDC9-EBE9-4362-9252-B22E63C3F106}";

// Name passed to MFCreateVirtualCamera. Windows appends " (Windows Virtual Camera)".
inline constexpr wchar_t kCameraName[] = L"SmoothCam";
} // namespace smoothcam
