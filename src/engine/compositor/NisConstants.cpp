#include "NisConstants.h"

#include <NIS_Config.h>

#include <cstring>

namespace smoothcam
{
static_assert(sizeof(NISConfig) == kNisConstantsSize);
static_assert(kPhaseCount == kNisCoefHeight && kFilterSize == kNisCoefWidth * 4);

const float* NisCoefScale()
{
    return &coef_scale[0][0];
}

const float* NisCoefUsm()
{
    return &coef_usm[0][0];
}

void ComputeNisConstants(void* out, float sharpness, uint32_t inputWidth, uint32_t inputHeight, uint32_t outputWidth,
                         uint32_t outputHeight)
{
    NISConfig config{};
    NVScalerUpdateConfig(config, sharpness, 0, 0, inputWidth, inputHeight, inputWidth, inputHeight, 0, 0, outputWidth,
                         outputHeight, outputWidth, outputHeight, NISHDRMode::None);
    memcpy(out, &config, sizeof(config));
}
} // namespace smoothcam
