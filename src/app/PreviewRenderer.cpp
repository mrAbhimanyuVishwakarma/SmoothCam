#include "PreviewRenderer.h"

#include "Log.h"

#include <wil/result.h>

#include <algorithm>

namespace smoothcam
{
HRESULT PreviewRenderer::Init(HWND hwnd)
{
    m_hwnd = hwnd;
    D2D1_FACTORY_OPTIONS options{};
    RETURN_IF_FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
                                       m_factory.put_void()));
    RETURN_IF_FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                         reinterpret_cast<IUnknown**>(m_dwrite.put())));
    SetDpi(GetDpiForWindow(hwnd));
    return S_OK;
}

void PreviewRenderer::SetDpi(UINT dpi)
{
    m_dpi = dpi ? dpi : 96;
    m_textFormat.reset();
    m_labelFormat.reset();
    if (SUCCEEDED(m_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                             13.0f * m_dpi / 96.0f, L"", &m_labelFormat)))
    {
        m_labelFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        m_labelFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    // The render target works in physical pixels, so size the font for this DPI.
    if (SUCCEEDED(m_dwrite->CreateTextFormat(L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                             DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                             15.0f * m_dpi / 96.0f, L"", &m_textFormat)))
    {
        m_textFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        m_textFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        m_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }
}

HRESULT PreviewRenderer::EnsureTarget()
{
    if (m_target)
        return S_OK;
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f,
        96.0f, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
    RETURN_IF_FAILED(m_factory->CreateDCRenderTarget(&props, &m_target));
    RETURN_IF_FAILED(m_target->QueryInterface(IID_PPV_ARGS(&m_context)));
    RETURN_IF_FAILED(m_target->CreateSolidColorBrush(D2D1::ColorF(0.62f, 0.62f, 0.64f), &m_textBrush));
    RETURN_IF_FAILED(m_target->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.55f), &m_gridBrush));
    RETURN_IF_FAILED(m_target->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.6f), &m_labelBack));
    RETURN_IF_FAILED(m_target->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f), &m_labelText));
    m_bitmap.reset();
    m_bitmapW = m_bitmapH = 0;
    return S_OK;
}

void PreviewRenderer::DiscardTarget()
{
    m_bitmap.reset();
    m_textBrush.reset();
    m_gridBrush.reset();
    m_labelBack.reset();
    m_labelText.reset();
    m_context.reset();
    m_target.reset();
    m_bitmapW = m_bitmapH = 0;
}

void PreviewRenderer::DrawGrid(const D2D1_RECT_F& r)
{
    if (m_grid == FramingGrid::Off || !m_gridBrush)
        return;
    const float stroke = std::max(1.0f, m_dpi / 96.0f);
    const float w = r.right - r.left;
    const float h = r.bottom - r.top;
    auto vline = [&](float f) {
        m_target->DrawLine(D2D1::Point2F(r.left + w * f, r.top), D2D1::Point2F(r.left + w * f, r.bottom),
                           m_gridBrush.get(), stroke);
    };
    auto hline = [&](float f) {
        m_target->DrawLine(D2D1::Point2F(r.left, r.top + h * f), D2D1::Point2F(r.right, r.top + h * f),
                           m_gridBrush.get(), stroke);
    };
    switch (m_grid)
    {
    case FramingGrid::RuleOfThirds:
        vline(1.0f / 3);
        vline(2.0f / 3);
        hline(1.0f / 3);
        hline(2.0f / 3);
        break;
    case FramingGrid::GoldenRatio:
        vline(0.382f);
        vline(0.618f);
        hline(0.382f);
        hline(0.618f);
        break;
    case FramingGrid::Crosshairs: {
        const float cx = r.left + w / 2, cy = r.top + h / 2, arm = std::min(w, h) * 0.05f;
        m_target->DrawLine(D2D1::Point2F(cx - arm, cy), D2D1::Point2F(cx + arm, cy), m_gridBrush.get(), stroke);
        m_target->DrawLine(D2D1::Point2F(cx, cy - arm), D2D1::Point2F(cx, cy + arm), m_gridBrush.get(), stroke);
        break;
    }
    case FramingGrid::Square: {
        const float side = std::min(w, h);
        const float x = r.left + (w - side) / 2, y = r.top + (h - side) / 2;
        m_target->DrawRectangle(D2D1::RectF(x, y, x + side, y + side), m_gridBrush.get(), stroke);
        break;
    }
    default:
        break;
    }
}

void PreviewRenderer::DrawLabels(const D2D1_RECT_F& r)
{
    if (!m_labelFormat || !m_labelBack || (m_labelLeft.empty() && m_labelRight.empty()))
        return;
    const float s = m_dpi / 96.0f;
    const float pad = 10.0f * s;
    const float h = 26.0f * s;
    auto label = [&](const std::wstring& text, bool right) {
        if (text.empty())
            return;
        wil::com_ptr_nothrow<IDWriteTextLayout> layout;
        if (FAILED(m_dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), m_labelFormat.get(),
                                              (r.right - r.left) / 2, h, &layout)))
            return;
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        const float w = m.width + 2 * pad;
        const float x = right ? r.right - pad - w : r.left + pad;
        const float y = r.top + pad;
        m_target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), 6 * s, 6 * s),
                                       m_labelBack.get());
        m_target->DrawTextLayout(D2D1::Point2F(x + pad, y), layout.get(), m_labelText.get());
    };
    label(m_labelLeft, false);
    label(m_labelRight, true);
}

void PreviewRenderer::Paint(HDC dc, const uint8_t* pixels, uint32_t width, uint32_t height, bool frameChanged,
                            const std::wstring& message)
{
    if (const HRESULT hr = EnsureTarget(); FAILED(hr))
    {
        SC_LOG_ERROR(L"Preview: creating the render target failed (0x%08X)", hr);
        return;
    }
    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    if (rc.right <= 0 || rc.bottom <= 0 || FAILED(m_target->BindDC(dc, &rc)))
        return;

    const float sw = static_cast<float>(rc.right);
    const float sh = static_cast<float>(rc.bottom);
    m_target->BeginDraw();
    m_target->Clear(D2D1::ColorF(0.04f, 0.04f, 0.045f));

    if (pixels && width && height)
    {
        if (!m_bitmap || m_bitmapW != width || m_bitmapH != height)
        {
            m_bitmap.reset();
            const auto bp = D2D1::BitmapProperties(
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96.0f, 96.0f);
            if (SUCCEEDED(m_target->CreateBitmap(D2D1::SizeU(width, height), bp, &m_bitmap)))
            {
                m_bitmapW = width;
                m_bitmapH = height;
                frameChanged = true;
            }
        }
        if (m_bitmap && frameChanged)
            m_bitmap->CopyFromMemory(nullptr, pixels, width * 4);

        if (m_bitmap)
        {
            // Letterbox, keeping the aspect ratio.
            const float scale = std::min(sw / width, sh / height);
            const float w = width * scale;
            const float h = height * scale;
            const D2D1_RECT_F dest = D2D1::RectF((sw - w) / 2, (sh - h) / 2, (sw + w) / 2, (sh + h) / 2);
            m_context->DrawBitmap(m_bitmap.get(), &dest, 1.0f, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, nullptr);
            DrawGrid(dest);
            DrawLabels(dest);
        }
    }
    else if (m_textFormat && !message.empty())
    {
        const float pad = 24.0f * m_dpi / 96.0f;
        m_target->DrawText(message.c_str(), static_cast<UINT32>(message.size()), m_textFormat.get(),
                           D2D1::RectF(pad, pad, sw - pad, sh - pad), m_textBrush.get());
    }

    const HRESULT hr = m_target->EndDraw();
    if (FAILED(hr))
    {
        SC_LOG_ERROR(L"Preview: EndDraw failed (0x%08X)", hr);
        DiscardTarget();
    }
}
} // namespace smoothcam
