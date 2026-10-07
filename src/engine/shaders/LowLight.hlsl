// Low-light boost: brightens dim scenes in software, so the camera can keep its
// full frame rate instead of stretching exposure.
//
// The gain comes from the frame's smoothed average brightness and is applied
// with a soft shoulder (y * g / (1 + (g - 1) * y)) that lifts shadows a lot,
// midtones a little and leaves highlights alone, so bright windows don't blow out.

Texture2D<float> InY : register(t0);
Texture2D<float2> InUV : register(t1);
Texture2D<float> Stats : register(t2); // 1x1 smoothed mean luma (limited range)

RWTexture2D<unorm float> OutY : register(u0);
RWTexture2D<unorm float2> OutUV : register(u1);

cbuffer Params : register(b0)
{
    uint2 Size;
    float Target;  // desired average brightness, 0..1 full range
    float MaxGain;
};

static const float kBlack = 16.0 / 255.0;
static const float kRange = 219.0 / 255.0;

float Boost(float y, float gain)
{
    const float v = saturate((y - kBlack) / kRange);
    const float b = v * gain / (1.0 + (gain - 1.0) * v);
    return b * kRange + kBlack;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint2 chromaSize = (Size + 1) / 2;
    if (any(id.xy >= chromaSize))
        return;

    const float mean = saturate((Stats[uint2(0, 0)] - kBlack) / kRange);
    // The shoulder curve's average lift is lower than the gain itself, so
    // overshoot the plain ratio a little; never darken.
    const float gain = clamp(Target / max(mean, 0.01) * 1.25, 1.0, MaxGain);

    const uint2 base = id.xy * 2;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const int2 p = int2(base) + int2(i & 1, i >> 1);
        if (all(p < int2(Size)))
            OutY[p] = Boost(InY[p], gain);
    }

    // Brightening washes colors out; give some saturation back (capped, since
    // chroma noise rises with it).
    const float sat = min(1.0 + 0.2 * (gain - 1.0), 1.4);
    OutUV[id.xy] = 0.5 + (InUV[id.xy] - 0.5) * sat;
}
