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
constexpr float kDeadZone = 0.06f;               // fraction of the crop size
constexpr float kFaceSmoothing = 0.35f;          // weight of each new detection
// Spring stiffness (rad/s): a move settles in about 6 / omega seconds. The
// close-up is a little calmer because the same head movement is bigger there.
constexpr float kOmegaWide = 3.6f;
constexpr float kOmegaClose = 3.0f;

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
    {
        m_initialized = m_initialized && mode != AutoFrame::Off;
        m_haveFace = false;
    }
    m_mode = mode;
}

CropRect AutoFramer::TargetFor(const float box[4], const CropRect& base, float sourceAspect, float outputAspect) const
{
    const float x0 = box[0], y0 = box[1], x1 = box[2], y1 = box[3];
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
        m_velocity = {0, 0, 0, 0};
        m_haveFace = false;
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
            // Frame all faces together, smoothing the detector's jitter.
            float box[4] = {1, 1, 0, 0};
            for (const auto& f : faces)
            {
                box[0] = std::min(box[0], f.x);
                box[1] = std::min(box[1], f.y);
                box[2] = std::max(box[2], f.x + f.w);
                box[3] = std::max(box[3], f.y + f.h);
            }
            for (int i = 0; i < 4; ++i)
                m_face[i] = m_haveFace ? m_face[i] + (box[i] - m_face[i]) * kFaceSmoothing : box[i];
            m_haveFace = true;
            const CropRect wanted = TargetFor(m_face, base, sourceAspect, outputAspect);
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
        m_haveFace = false;
    }
    m_target = Clamp(m_target, base);

    // Critically damped spring towards the target, in small fixed steps so it
    // behaves the same at any frame rate.
    float dt = std::clamp(static_cast<float>(now - m_lastUpdate) / kHnsPerSecond, 0.0f, 0.25f);
    m_lastUpdate = now;
    const float omega = m_mode == AutoFrame::Close ? kOmegaClose : kOmegaWide;
    float* pos[4] = {&m_current.x, &m_current.y, &m_current.w, &m_current.h};
    float* vel[4] = {&m_velocity.x, &m_velocity.y, &m_velocity.w, &m_velocity.h};
    const float goal[4] = {m_target.x, m_target.y, m_target.w, m_target.h};
    while (dt > 0.0f)
    {
        const float h = std::min(dt, 1.0f / 240.0f);
        for (int i = 0; i < 4; ++i)
        {
            const float accel = omega * omega * (goal[i] - *pos[i]) - 2.0f * omega * *vel[i];
            *vel[i] += accel * h;
            *pos[i] += *vel[i] * h;
        }
        dt -= h;
    }
    m_current = Clamp(m_current, base);
    return m_current;
}
} // namespace smoothcam
