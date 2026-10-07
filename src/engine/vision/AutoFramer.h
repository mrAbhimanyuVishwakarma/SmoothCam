#pragma once

#include "Clock.h"
#include "Settings.h"
#include "vision/Vision.h"

#include <vector>

namespace smoothcam
{
// Region of the source frame to show, in normalized source coordinates.
struct CropRect
{
    float x = 0;
    float y = 0;
    float w = 1;
    float h = 1;
};

// Decides where to point a virtual camera so the face stays framed, and moves
// there smoothly - like a camera operator, not a jittery tracker:
//   * a dead zone ignores small head movements,
//   * the crop eases towards its target (no jumps),
//   * if the face is lost for a while it slowly zooms back out.
class AutoFramer
{
public:
    void SetMode(AutoFrame mode);
    AutoFrame Mode() const { return m_mode; }

    // `base` is the full-frame crop at the output's aspect ratio. `sourceAspect`
    // and `outputAspect` are width / height in pixels.
    CropRect Update(const std::vector<FaceBox>& faces, Hns facesTime, Hns now, const CropRect& base,
                    float sourceAspect, float outputAspect);

private:
    CropRect TargetFor(const std::vector<FaceBox>& faces, const CropRect& base, float sourceAspect,
                       float outputAspect) const;

    AutoFrame m_mode = AutoFrame::Off;
    CropRect m_current;
    CropRect m_target;
    Hns m_lastUpdate = 0;
    Hns m_lastFace = 0;
    Hns m_lastFacesTime = 0;
    bool m_initialized = false;
};
} // namespace smoothcam
