#pragma once

#include <cstdint>

namespace smoothcam
{
// Constant block and filter tables for the NIS shader, computed with NVIDIA's
// reference code (NIS_Config.h), kept behind this small interface like FSR's.
constexpr uint32_t kNisBlockWidth = 32;
constexpr uint32_t kNisBlockHeight = 24;
constexpr uint32_t kNisConstantsSize = 256; // sizeof(NISConfig)

// The tables are kPhaseCount (64) rows of kFilterSize (8) floats, uploaded as
// 2x64 RGBA32F textures.
constexpr uint32_t kNisCoefWidth = 2;
constexpr uint32_t kNisCoefHeight = 64;
const float* NisCoefScale();
const float* NisCoefUsm();

// `sharpness` 0..1 (NVIDIA's slider; 0 = no sharpening).
void ComputeNisConstants(void* out, float sharpness, uint32_t inputWidth, uint32_t inputHeight, uint32_t outputWidth,
                         uint32_t outputHeight);

// NIS handles up to 2x per axis.
inline bool NisCanScale(uint32_t inW, uint32_t inH, uint32_t outW, uint32_t outH)
{
    return inW * 2 >= outW && inH * 2 >= outH && inW <= outW && inH <= outH;
}
} // namespace smoothcam
