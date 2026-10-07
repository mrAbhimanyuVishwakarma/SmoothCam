// Shrinks a camera frame (NV12 planes) to a small RGB image for the person
// segmentation model. Area-averages a 4x4 pattern so thin detail doesn't alias.

#include "Common.hlsli"

Texture2D<float> InY : register(t0);
Texture2D<float2> InUV : register(t1);
RWTexture2D<unorm float4> Out : register(u0);

cbuffer Params : register(b0)
{
    uint2 OutSize;
    uint Bt709;
    uint Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= OutSize))
        return;
    float y = 0;
    [unroll]
    for (int j = 0; j < 4; ++j)
    {
        [unroll]
        for (int i = 0; i < 4; ++i)
        {
            const float2 uv = (float2(id.xy) + (float2(i, j) + 0.5) / 4.0) / float2(OutSize);
            y += InY.SampleLevel(LinearClamp, uv, 0);
        }
    }
    y = (y / 16.0 - 16.0 / 255.0) * (255.0 / 219.0);
    const float2 c = (InUV.SampleLevel(LinearClamp, (float2(id.xy) + 0.5) / float2(OutSize), 0) - 128.0 / 255.0) *
                     (255.0 / 224.0);
    float3 rgb;
    if (Bt709)
        rgb = float3(y + 1.5748 * c.y, y - 0.1873 * c.x - 0.4681 * c.y, y + 1.8556 * c.x);
    else
        rgb = float3(y + 1.402 * c.y, y - 0.344136 * c.x - 0.714136 * c.y, y + 1.772 * c.x);
    Out[id.xy] = float4(saturate(rgb), 1);
}
