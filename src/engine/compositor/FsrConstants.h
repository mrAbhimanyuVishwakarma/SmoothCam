#pragma once

#include <cstdint>

namespace smoothcam
{
// Constant blocks for the FSR 1 shaders, computed with AMD's reference code.
// Kept behind this small interface because ffx_a.h defines a lot of macros.
void ComputeEasuConstants(uint32_t out[16], float inputWidth, float inputHeight, float outputWidth,
                          float outputHeight);

// `sharpness` is in stops: 0 = strongest, each +1 halves the effect.
void ComputeRcasConstants(uint32_t out[4], float sharpness);
} // namespace smoothcam
