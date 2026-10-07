// AMD FidelityFX Super Resolution 1 - RCAS (robust contrast-adaptive
// sharpening) on the luma plane. The denoise variant backs off on noisy
// pixels, which matters for webcams in dim rooms.

#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"

Texture2D<float> InputY : register(t0);
RWTexture2D<unorm float> OutputY : register(u0);

cbuffer Params : register(b0)
{
    uint4 Const0;
    uint2 Size;
    uint2 Pad;
};

#define FSR_RCAS_F 1
#define FSR_RCAS_DENOISE 1
AF4 FsrRcasLoadF(ASU2 p)
{
    // Clamp: out-of-range loads return 0 and would darken the border.
    const float y = InputY.Load(int3(clamp(p, int2(0, 0), int2(Size) - 1), 0));
    return AF4(y, y, y, 1);
}
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}

#include "ffx_fsr1.h"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Size))
        return;
    AF3 c;
    FsrRcasF(c.r, c.g, c.b, id.xy, Const0);
    OutputY[id.xy] = c.r;
}
