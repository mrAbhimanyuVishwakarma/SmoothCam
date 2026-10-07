#include "AutoFramer.h"

#include <algorithm>
#include <cmath>

namespace smoothcam
{
namespace
{
// How much of the picture's height the face should take, and the most we zoom.
// FaceDetector boxes are generous (hairline to below the chin), so these look
// tighter on paper than on screen: at a laptop distance the box is already
// ~0.5 of the frame, and a close-up should still visibly tighten from there.
struct Framing
{
    float faceFraction;
    float maxZoom;
};

Framing FramingFor(AutoFrame mode)
{
    return mode == AutoFrame::Close ? Framing{0.65f, 2.5f} : Framing{0.30f, 1.6f};
}

constexpr Hns kLoseFaceAfter = 1500 * kHnsPerMs; // then drift back to the full view
constexpr float kMoveTime = 0.45f;               // seconds to cover ~63% of a move
constexpr float kDeadZone = 0.06f;               // fraction of the crop size

CropRect Clamp(CropRect r, const CropRect& base)
{
    r.w = std::min(r.w, base.w);
    r.h = std::min(r.h, base.h);
    r.x = std::clamp(r.x, base.x, base.x + base.w - r.w);
    r.y = std::clamp(r.y, base.y, base.y + base.h - r.h);
    return r;
}
} // namespace

void AutoFramer::SetMode(AutoFrame mode)
{
    if (mode != m_mode)
        m_initialized = m_initialized && mode != AutoFrame::Off;
    m_mode = mode;
}

CropRect AutoFramer::TargetFor(const std::vector<FaceBox>& faces, const CropRect& base, float sourceAspect,
                               float outputAspect) const
{
    // Frame all faces together.
    float x0 = 1, y0 = 1, x1 = 0, y1 = 0;
    for (const auto& f : faces)
    {
        x0 = std::min(x0, f.x);
        y0 = std::min(y0, f.y);
        x1 = std::max(x1, f.x + f.w);
        y1 = std::max(y1, f.y + f.h);
    }
    const Framing framing = FramingFor(m_mode);
    float h = (y1 - y0) / framing.faceFraction;
    h = std::clamp(h, base.h / framing.maxZoom, base.h);
    // Same pixel aspect as the output: w_norm * srcW / (h_norm * srcH) = outputAspect.
    const float w = h * outputAspect / sourceAspect;

    CropRect r;
    r.w = w;
    r.h = h;
    r.x = (x0 + x1) * 0.5f - w * 0.5f;
    // Eyes a bit above the middle: put the face center ~42% from the top.
    r.y = (y0 + y1) * 0.5f - h * 0.42f;
    return Clamp(r, base);
}

CropRect AutoFramer::Update(const std::vector<FaceBox>& faces, Hns facesTime, Hns now, const CropRect& base,
                            float sourceAspect, float outputAspect)
{
    if (m_mode == AutoFrame::Off)
    {
        m_initialized = false;
        return base;
    }
    if (!m_initialized)
    {
        m_current = m_target = base;
        m_lastUpdate = now;
        m_initialized = true;
    }

    const bool newDetection = facesTime != m_lastFacesTime;
    m_lastFacesTime = facesTime;
    if (!faces.empty() && now - facesTime < kLoseFaceAfter)
    {
        m_lastFace = facesTime;
        if (newDetection)
        {
            const CropRect wanted = TargetFor(faces, base, sourceAspect, outputAspect);
            // Dead zone: only re-aim when the face has really moved or changed size.
            const float moved = std::max(std::abs((wanted.x + wanted.w * 0.5f) - (m_target.x + m_target.w * 0.5f)),
                                         std::abs((wanted.y + wanted.h * 0.5f) - (m_target.y + m_target.h * 0.5f)));
            const float resized = std::abs(wanted.h - m_target.h) / m_target.h;
            if (moved > kDeadZone * m_target.w || resized > 0.12f)
                m_target = wanted;
        }
    }
    else if (now - m_lastFace > kLoseFaceAfter)
    {
        m_target = base;
    }
    m_target = Clamp(m_target, base);

    // Ease towards the target, frame-rate independent.
    const float dt = std::clamp(static_cast<float>(now - m_lastUpdate) / kHnsPerSecond, 0.0f, 0.25f);
    m_lastUpdate = now;
    const float k = 1.0f - std::exp(-dt / kMoveTime);
    m_current.x += (m_target.x - m_current.x) * k;
    m_current.y += (m_target.y - m_current.y) * k;
    m_current.w += (m_target.w - m_current.w) * k;
    m_current.h += (m_target.h - m_current.h) * k;
    m_current = Clamp(m_current, base);
    return m_current;
}
} // namespace smoothcam
