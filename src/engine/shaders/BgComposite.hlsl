// Mixes the sharp frame with its blurred copy using the person mask from the
// segmentation model: person stays sharp, background goes soft.
//
// The model's mask is small (256x144), so stretched over a 1080p or 4K frame
// its edge is a soft blob that doesn't follow hair or shoulders. Near the edge
// we therefore upsample it "joint bilaterally": each nearby mask texel counts
// more when the picture there looks like the picture at this pixel, which
// snaps the edge onto the real outline. Away from edges a plain bilinear
// sample is used, so this costs little.

#include "Common.hlsli"

Texture2D<float> InY : register(t0);
Texture2D<float2> InUV : register(t1);
Texture2D<float4> Blurred : register(t2); // quarter size, background-only premultiplied (Y, U, V) * w, w
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

static const float kSigmaY = 0.07;       // how different (in luma) still counts as "the same surface"
static const float kSpatialRadius = 1.6; // in mask texels

// Average picture brightness over a mask texel's footprint, from the output frame.
float GuideAt(float2 srcUv, float2 texelOut)
{
    const float2 o = (srcUv - CropOffset) / CropScale; // normalized output coordinates
    const float2 q = texelOut * 0.25 / float2(Size);
    return 0.25 * (InY.SampleLevel(LinearClamp, o + float2(-q.x, -q.y), 0) +
                   InY.SampleLevel(LinearClamp, o + float2(q.x, -q.y), 0) +
                   InY.SampleLevel(LinearClamp, o + float2(-q.x, q.y), 0) +
                   InY.SampleLevel(LinearClamp, o + float2(q.x, q.y), 0));
}

// The blurred background at a point: premultiplied by "background-ness", so
// divide it back out. Deep inside the person there's no background to show;
// those pixels are sharp anyway, so `fallback` is fine there.
float3 BackgroundAt(float2 uv, float3 fallback)
{
    const float4 b = Blurred.SampleLevel(LinearClamp, uv, 0);
    return b.w > 0.02 ? b.xyz / b.w : fallback;
}

float PersonAt(float2 outPixel)
{
    uint mw, mh;
    Mask.GetDimensions(mw, mh);
    const float2 maskSize = float2(mw, mh);
    const float2 src = CropOffset + (outPixel / float2(Size)) * CropScale;
    const float plain = Mask.SampleLevel(LinearClamp, src, 0);

    // Uniform neighbourhood (inside the person or the background): done.
    const float4 g = Mask.Gather(LinearClamp, src);
    const float lo = min(min(g.x, g.y), min(g.z, g.w));
    const float hi = max(max(g.x, g.y), max(g.z, g.w));
    if (hi - lo < 0.08)
        return smoothstep(Edge0, Edge1, plain);

    // Near an edge: joint bilateral upsampling over the 4x4 nearest texels.
    const float2 texelOut = float2(Size) / (maskSize * CropScale); // output pixels per mask texel
    const float here = InY.SampleLevel(LinearClamp, (outPixel + 0.5) / float2(Size), 0); // 2x2 average
    const float2 mp = src * maskSize - 0.5;
    const float2 b = floor(mp);
    const float2 f = mp - b;
    float sumW = 0, sumM = 0;
    [unroll] for (int j = -1; j <= 2; ++j)
    {
        [unroll] for (int i = -1; i <= 2; ++i)
        {
            const float2 t = b + float2(i, j);
            const float d = length(float2(i, j) - f);
            const float ws = max(0.0, kSpatialRadius - d);
            const float2 tuv = (t + 0.5) / maskSize;
            const float dy = GuideAt(tuv, texelOut) - here;
            const float wr = exp(-dy * dy / (2.0 * kSigmaY * kSigmaY));
            const float m = Mask.Load(int3(clamp(int2(t), int2(0, 0), int2(mw, mh) - 1), 0));
            sumW += ws * wr;
            sumM += ws * wr * m;
        }
    }
    const float refined = sumW > 1e-4 ? sumM / sumW : plain;
    return smoothstep(Edge0, Edge1, refined);
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint2 chromaSize = (Size + 1) / 2;
    if (any(id.xy >= chromaSize))
        return;

    const uint2 base = id.xy * 2;
    float mSum = 0;
    int mCount = 0;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        const int2 p = int2(base) + int2(i & 1, i >> 1);
        if (any(p >= int2(Size)))
            continue;
        const float2 center = float2(p) + 0.5;
        const float m = PersonAt(center);
        mSum += m;
        ++mCount;
        const float blurred = BackgroundAt(center / float2(Size), float3(InY[p], 0.5, 0.5)).x;
        OutY[p] = lerp(blurred, InY[p], m);
    }

    // Chroma follows the average of its four luma pixels.
    const float2 c = float2(base) + 1.0;
    const float m = mCount > 0 ? mSum / mCount : 0.0;
    const float2 blurredUV = BackgroundAt(c / float2(Size), float3(0.5, InUV[id.xy])).yz;
    OutUV[id.xy] = lerp(blurredUV, InUV[id.xy], m);
}
