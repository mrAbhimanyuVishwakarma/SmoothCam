// Mixes the sharp frame with its blurred copy using the person mask from the
// segmentation model: person stays sharp, background goes soft.

#include "Common.hlsli"

Texture2D<float> InY : register(t0);
Texture2D<float2> InUV : register(t1);
Texture2D<float4> Blurred : register(t2); // quarter size (Y, U, V)
Texture2D<float> Mask : register(t3);     // person probability, in *source* frame coordinates

RWTexture2D<unorm float> OutY : register(u0);
RWTexture2D<unorm float2> OutUV : register(u1);

cbuffer Params : register(b0)
{
    uint2 Size;
    float2 CropOffset; // output -> source mapping (same as the compositor's crop)
    float2 CropScale;
    float Edge0;       // mask feather range
    float Edge1;
};

float PersonAt(float2 outPixel)
{
    const float2 src = CropOffset + (outPixel / float2(Size)) * CropScale;
    return smoothstep(Edge0, Edge1, Mask.SampleLevel(LinearClamp, src, 0));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint2 chromaSize = (Size + 1) / 2;
    if (any(id.xy >= chromaSize))
        return;

    const uint2 base = id.xy * 2;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const int2 p = int2(base) + int2(i & 1, i >> 1);
        if (any(p >= int2(Size)))
            continue;
        const float2 center = float2(p) + 0.5;
        const float m = PersonAt(center);
        const float blurred = Blurred.SampleLevel(LinearClamp, center / float2(Size), 0).x;
        OutY[p] = lerp(blurred, InY[p], m);
    }

    const float2 c = float2(base) + 1.0;
    const float m = PersonAt(c);
    const float2 blurredUV = Blurred.SampleLevel(LinearClamp, c / float2(Size), 0).yz;
    OutUV[id.xy] = lerp(blurredUV, InUV[id.xy], m);
}
