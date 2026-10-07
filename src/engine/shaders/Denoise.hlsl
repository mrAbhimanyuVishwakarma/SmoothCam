// Noise / grain reduction for a new camera frame.
//
// Temporal: blend each pixel with the previous (already cleaned) frame, fetched
// along the motion vector so moving things don't smear. The blend backs off
// where the two disagree by more than the expected noise, which is what keeps
// real motion and edges sharp. Because the history is itself filtered, this
// averages over several frames.
//
// Spatial (medium/high): a small edge-preserving bilateral on luma and a wider
// one on chroma, where webcam noise is the most blotchy.

#include "Common.hlsli"

Texture2D<float> RawY : register(t0);
Texture2D<float2> RawUV : register(t1);
Texture2D<float> HistY : register(t2);
Texture2D<float2> HistUV : register(t3);
Texture2D<float4> FlowB : register(t4); // this frame -> previous frame, source pixels, z = confidence

RWTexture2D<unorm float> OutY : register(u0);
RWTexture2D<unorm float2> OutUV : register(u1);

cbuffer Params : register(b0)
{
    uint2 Size;
    float Temporal; // max blend weight towards the history, 0 = off
    float Spatial;  // 0 = off, 1 = strongest
    uint HasHistory;
    uint HasFlow;
    float Sigma;    // expected noise amplitude (0..1 luma)
    float Pad;
};

float SpatialY(int2 p)
{
    const int2 lim = int2(Size) - 1;
    const float c = RawY[clamp(p, 0, lim)];
    if (Spatial <= 0)
        return c;
    const float range = 0.015 + 0.035 * Spatial;
    float sum = 0;
    float weight = 0;
    [unroll]
    for (int dy = -1; dy <= 1; ++dy)
    {
        [unroll]
        for (int dx = -1; dx <= 1; ++dx)
        {
            const float v = RawY[clamp(p + int2(dx, dy), 0, lim)];
            const float d = v - c;
            const float w = exp(-(dx * dx + dy * dy) * 0.5) * exp(-(d * d) / (2.0 * range * range));
            sum += v * w;
            weight += w;
        }
    }
    return sum / weight;
}

float2 SpatialUV(int2 p)
{
    const uint2 chromaSize = (Size + 1) / 2;
    const int2 lim = int2(chromaSize) - 1;
    const float2 c = RawUV[clamp(p, 0, lim)];
    if (Spatial <= 0)
        return c;
    const float range = 0.02 + 0.05 * Spatial;
    float2 sum = 0;
    float weight = 0;
    [unroll]
    for (int dy = -2; dy <= 2; ++dy)
    {
        [unroll]
        for (int dx = -2; dx <= 2; ++dx)
        {
            const float2 v = RawUV[clamp(p + int2(dx, dy), 0, lim)];
            const float2 d = v - c;
            const float w = exp(-(dx * dx + dy * dy) / 4.0) * exp(-dot(d, d) / (2.0 * range * range));
            sum += v * w;
            weight += w;
        }
    }
    return sum / weight;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint2 chromaSize = (Size + 1) / 2;
    if (any(id.xy >= chromaSize))
        return;

    const uint2 base = id.xy * 2;
    const float2 center = float2(base) + 1.0;
    float2 offset = 0;
    float conf = 1;
    if (HasFlow)
    {
        const float4 f = FlowB.SampleLevel(LinearClamp, center / float2(Size), 0);
        offset = f.xy;
        conf = f.z;
    }
    const float motionTrust = lerp(0.35, 1.0, saturate(conf));

    float lumaDiff = 0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const int2 p = int2(base) + int2(i & 1, i >> 1);
        if (any(p >= int2(Size)))
            continue;
        float y = SpatialY(p);
        if (HasHistory)
        {
            const float h = HistY.SampleLevel(LinearClamp, (float2(p) + 0.5 + offset) / float2(Size), 0);
            const float d = y - h;
            lumaDiff += abs(d);
            const float w = Temporal * motionTrust * exp(-(d * d) / (2.0 * Sigma * Sigma));
            y = lerp(y, h, w);
        }
        OutY[p] = y;
    }

    float2 uv = SpatialUV(int2(id.xy));
    if (HasHistory)
    {
        const float2 h = HistUV.SampleLevel(LinearClamp, (center + offset) / float2(Size), 0);
        const float2 d = uv - h;
        // Chroma noise is larger than luma noise; also trust it less where luma moved.
        const float s = Sigma * 1.3;
        const float w = Temporal * motionTrust * exp(-dot(d, d) / (2.0 * s * s)) *
                        exp(-(lumaDiff * 0.25) * (lumaDiff * 0.25) / (2.0 * Sigma * Sigma));
        uv = lerp(uv, h, w);
    }
    OutUV[id.xy] = uv;
}
