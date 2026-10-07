#pragma once

#include <windows.h>
#include <d2d1_1.h>
#include <dwrite.h>

#include <wil/com.h>

#include <cstdint>
#include <string>

namespace smoothcam
{
// Composition guides drawn over the preview (never sent to other apps).
enum class FramingGrid
{
    Off,
    RuleOfThirds,
    GoldenRatio,
    Crosshairs,
    Square,
};

// Draws the live preview with Direct2D: the GPU does the (high-quality cubic)
// scaling, so a 720p stream still looks good in a full-screen window.
//
// It renders through a DC render target bound to the WM_PAINT device context.
// An HWND render target would avoid one copy, but it doesn't show up reliably
// in dialog child windows on hybrid-GPU laptops, while this path always does.
class PreviewRenderer
{
public:
    void SetGrid(FramingGrid grid) { m_grid = grid; }

    HRESULT Init(HWND hwnd);
    void SetDpi(UINT dpi);

    // `pixels` (top-down BGRA) may be null when there's no frame; then
    // `message` is shown instead. `frameChanged` avoids re-uploading the same frame.
    void Paint(HDC dc, const uint8_t* pixels, uint32_t width, uint32_t height, bool frameChanged,
               const std::wstring& message);

private:
    HRESULT EnsureTarget();
    void DiscardTarget();
    void DrawGrid(const D2D1_RECT_F& video);

    HWND m_hwnd = nullptr;
    UINT m_dpi = 96;
    wil::com_ptr_nothrow<ID2D1Factory1> m_factory;
    wil::com_ptr_nothrow<IDWriteFactory> m_dwrite;
    wil::com_ptr_nothrow<IDWriteTextFormat> m_textFormat;
    wil::com_ptr_nothrow<ID2D1DCRenderTarget> m_target;
    wil::com_ptr_nothrow<ID2D1DeviceContext> m_context;
    wil::com_ptr_nothrow<ID2D1SolidColorBrush> m_textBrush;
    wil::com_ptr_nothrow<ID2D1SolidColorBrush> m_gridBrush;
    FramingGrid m_grid = FramingGrid::Off;
    wil::com_ptr_nothrow<ID2D1Bitmap> m_bitmap;
    uint32_t m_bitmapW = 0;
    uint32_t m_bitmapH = 0;
};
} // namespace smoothcam
