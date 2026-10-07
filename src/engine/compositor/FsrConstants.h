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

// AMD FidelityFX CAS (upscale + sharpen). `sharpness` 0..1 (1 = strongest).
void ComputeCasConstants(uint32_t out[8], float sharpness, float inputWidth, float inputHeight, float outputWidth,
                         float outputHeight);

// CAS handles up to 4x the pixel count (2x per axis).
bool CasCanScale(float inputWidth, float inputHeight, float outputWidth, float outputHeight);
} // namespace smoothcam
