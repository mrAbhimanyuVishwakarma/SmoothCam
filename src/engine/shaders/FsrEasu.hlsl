// AMD FidelityFX Super Resolution 1 - EASU (edge-adaptive spatial upsampling),
// run on the luma plane. Chroma is upscaled separately (ScaleUV.hlsl): the eye
// gets its sense of detail from luma, so that's where the work goes.
//
// Spatial only - it looks at one frame, so it adds no frames of latency.

#define A_GPU 1
#define A_HLSL 1
#include "ffx_a.h"

Texture2D<float> InputY : register(t0);
RWTexture2D<unorm float> OutputY : register(u0);
SamplerState LinearClamp : register(s0);

cbuffer Params : register(b0)
{
    uint4 Const0;
    uint4 Const1;
    uint4 Const2;
    uint4 Const3;
    uint2 OutSize;
    uint2 Pad;
};

// EASU works on RGB; with a single plane every channel is the same.
#define FSR_EASU_F 1
AF4 FsrEasuRF(AF2 p) { return InputY.GatherRed(LinearClamp, p); }
AF4 FsrEasuGF(AF2 p) { return InputY.GatherRed(LinearClamp, p); }
AF4 FsrEasuBF(AF2 p) { return InputY.GatherRed(LinearClamp, p); }

#include "ffx_fsr1.h"

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= OutSize))
        return;
    AF3 c;
    FsrEasuF(c, id.xy, Const0, Const1, Const2, Const3);
    OutputY[id.xy] = c.r;
}
