// Shrinks the luma plane to a small grayscale image for face detection.

#include "Common.hlsli"

Texture2D<float> InY : register(t0);
RWTexture2D<unorm float> Out : register(u0);

cbuffer Params : register(b0)
{
    uint2 OutSize;
    uint2 Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= OutSize))
        return;
    float y = 0;
    [unroll]
    for (int j = 0; j < 2; ++j)
    {
        [unroll]
        for (int i = 0; i < 2; ++i)
        {
            const float2 uv = (float2(id.xy) + (float2(i, j) + 0.5) / 2.0) / float2(OutSize);
            y += InY.SampleLevel(LinearClamp, uv, 0);
        }
    }
    // Stretch limited range to full range so the detector sees normal contrast.
    Out[id.xy] = saturate((y * 0.25 - 16.0 / 255.0) * (255.0 / 219.0));
}
