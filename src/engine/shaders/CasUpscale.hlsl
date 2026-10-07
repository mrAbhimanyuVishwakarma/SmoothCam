// AMD FidelityFX CAS (contrast adaptive sharpening) in its scaling mode: a
// light upscale + sharpen in one pass, run on the luma plane. Supports up to
// 2x per axis; bigger steps use FSR instead.

#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"

Texture2D<float> InputY : register(t0);
RWTexture2D<unorm float> OutputY : register(u0);

cbuffer Params : register(b0)
{
    uint4 Const0;
    uint4 Const1;
    uint2 OutSize;
    uint2 Pad;
};

// CAS works on RGB; with a single plane every channel is the same. Loads are
// clamped because out-of-range loads return 0 and would darken the border.
AF3 CasLoad(ASU2 p)
{
    uint w, h;
    InputY.GetDimensions(w, h);
    const float y = InputY.Load(int3(clamp(p, int2(0, 0), int2(w, h) - 1), 0));
    return AF3(y, y, y);
}
void CasInput(inout AF1 r, inout AF1 g, inout AF1 b) {}

#include "ffx_cas.h"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= OutSize))
        return;
    AF3 c;
    CasFilter(c.r, c.g, c.b, id.xy, Const0, Const1, false);
    OutputY[id.xy] = c.r;
}
