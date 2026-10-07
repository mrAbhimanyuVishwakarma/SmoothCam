#include "FsrConstants.h"

#include <cmath>
#include <cstring>

#pragma warning(push)
#pragma warning(disable : 4100 4189 4244 4245 4305 4456 4505 4701 4706)
#define A_CPU 1
#include "ffx_a.h"
#include "ffx_fsr1.h"
#include "ffx_cas.h"
#pragma warning(pop)

namespace smoothcam
{
void ComputeEasuConstants(uint32_t out[16], float inputWidth, float inputHeight, float outputWidth,
                          float outputHeight)
{
    varAU4(con0);
    varAU4(con1);
    varAU4(con2);
    varAU4(con3);
    FsrEasuCon(con0, con1, con2, con3, inputWidth, inputHeight, inputWidth, inputHeight, outputWidth, outputHeight);
    memcpy(out + 0, con0, 16);
    memcpy(out + 4, con1, 16);
    memcpy(out + 8, con2, 16);
    memcpy(out + 12, con3, 16);
}

void ComputeRcasConstants(uint32_t out[4], float sharpness)
{
    varAU4(con);
    FsrRcasCon(con, sharpness);
    memcpy(out, con, 16);
}

void ComputeCasConstants(uint32_t out[8], float sharpness, float inputWidth, float inputHeight, float outputWidth,
                         float outputHeight)
{
    varAU4(con0);
    varAU4(con1);
    CasSetup(con0, con1, sharpness, inputWidth, inputHeight, outputWidth, outputHeight);
    memcpy(out + 0, con0, 16);
    memcpy(out + 4, con1, 16);
}

bool CasCanScale(float inputWidth, float inputHeight, float outputWidth, float outputHeight)
{
    return inputWidth * 2 >= outputWidth && inputHeight * 2 >= outputHeight &&
           CasSupportScaling(outputWidth, outputHeight, inputWidth, inputHeight);
}
} // namespace smoothcam
