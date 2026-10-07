// Builds one output frame (NV12 planes) from two source frames and their
// motion fields.
//
// Each thread handles one chroma sample = a 2x2 block of luma pixels, which
// share one displacement (the fields are far coarser than that anyway).
//
// Modes
//   0  Hold        - copy Prev (scaled/cropped to the output)
//   1  Blend       - cross-fade Prev -> Next at Alpha (no motion estimation)
//   2  Interpolate - motion-compensated, Prev at time 0, Next at time 1
//   3  Extrapolate - continue Next's motion forward by Alpha intervals

#include "Common.hlsli"

Texture2D<float> PrevY : register(t0);
Texture2D<float2> PrevUV : register(t1);
Texture2D<float> NextY : register(t2);
Texture2D<float2> NextUV : register(t3);
Texture2D<float4> FlowF : register(t4); // Prev -> Next, Prev grid
Texture2D<float4> FlowB : register(t5); // Next -> Prev, Next grid

RWTexture2D<unorm float> OutY : register(u0);
RWTexture2D<unorm float2> OutUV : register(u1);

cbuffer Params : register(b0)
{
    uint2 OutSize;      // output luma size
    float2 SrcSize;     // source luma size
    float2 CropOffset;  // source UV of the output's top-left corner
    float2 CropScale;   // source UV span of the output
    float Alpha;
    uint Mode;
    uint Compare;       // 1 = left half shows the nearest real frame
    float ConfGain;
};

// Fixed-point search for the source position whose content lands on `c`.
//   prev: x + a * F(x) = c   ->  x = c - a * F(x)
float2 SolveWarp(Texture2D<float4> field, float2 c, float a, out float conf)
{
    float2 x = c;
    float4 f = 0;
    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        f = field.SampleLevel(LinearClamp, x / SrcSize, 0);
        x = c - a * f.xy;
    }
    conf = f.z;
    return x;
}

void Store(uint2 id, float4 y, float2 uv)
{
    const uint2 base = id * 2;
    OutY[base] = y.x;
    if (base.x + 1 < OutSize.x)
        OutY[base + uint2(1, 0)] = y.y;
    if (base.y + 1 < OutSize.y)
    {
        OutY[base + uint2(0, 1)] = y.z;
        if (base.x + 1 < OutSize.x)
            OutY[base + uint2(1, 1)] = y.w;
    }
    OutUV[id] = uv;
}

float4 SampleQuadY(Texture2D<float> tex, float2 center, float2 lumaStep)
{
    // Positions of the 4 output luma pixels around the chroma sample, in source pixels.
    const float2 o = lumaStep * 0.5;
    return float4(tex.SampleLevel(LinearClamp, (center + float2(-o.x, -o.y)) / SrcSize, 0),
                  tex.SampleLevel(LinearClamp, (center + float2(o.x, -o.y)) / SrcSize, 0),
                  tex.SampleLevel(LinearClamp, (center + float2(-o.x, o.y)) / SrcSize, 0),
                  tex.SampleLevel(LinearClamp, (center + float2(o.x, o.y)) / SrcSize, 0));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint2 chromaSize = (OutSize + 1) / 2;
    if (any(id.xy >= chromaSize))
        return;

    // Center of this 2x2 quad, in output pixels, then in source pixels.
    const float2 outCenter = float2(id.xy) * 2.0 + 1.0;
    const float2 uv = CropOffset + (outCenter / float2(OutSize)) * CropScale;
    const float2 c = uv * SrcSize;
    const float2 lumaStep = CropScale * SrcSize / float2(OutSize); // source pixels per output pixel

    uint mode = Mode;
    if (Compare != 0 && outCenter.x < OutSize.x * 0.5)
        mode = (mode == 3) ? 0xFF : 0xFE; // nearest real frame, see below

    float4 y;
    float2 chroma;

    if (mode == 0 || (mode == 0xFE && Alpha < 0.5))
    {
        y = SampleQuadY(PrevY, c, lumaStep);
        chroma = PrevUV.SampleLevel(LinearClamp, uv, 0);
    }
    else if (mode == 0xFE || mode == 0xFF)
    {
        y = SampleQuadY(NextY, c, lumaStep);
        chroma = NextUV.SampleLevel(LinearClamp, uv, 0);
    }
    else if (mode == 1)
    {
        y = lerp(SampleQuadY(PrevY, c, lumaStep), SampleQuadY(NextY, c, lumaStep), Alpha);
        chroma = lerp(PrevUV.SampleLevel(LinearClamp, uv, 0), NextUV.SampleLevel(LinearClamp, uv, 0), Alpha);
    }
    else if (mode == 2)
    {
        float cf, cb;
        const float2 x = SolveWarp(FlowF, c, Alpha, cf);
        const float2 z = SolveWarp(FlowB, c, 1.0 - Alpha, cb);

        const float4 ya = SampleQuadY(PrevY, x, lumaStep);
        const float4 yb = SampleQuadY(NextY, z, lumaStep);
        const float2 ca = PrevUV.SampleLevel(LinearClamp, x / SrcSize, 0);
        const float2 cbv = NextUV.SampleLevel(LinearClamp, z / SrcSize, 0);

        // Temporal weight, nudged towards whichever side matched better.
        const float w0 = (1.0 - Alpha) * (cf + 0.05);
        const float w1 = Alpha * (cb + 0.05);
        const float t = w1 / (w0 + w1);
        float4 warpedY = lerp(ya, yb, t);
        float2 warpedC = lerp(ca, cbv, t);

        // Where neither side is trustworthy, fall back to a plain cross-fade.
        const float g = saturate(max(cf, cb) * ConfGain);
        const float4 blendY = lerp(SampleQuadY(PrevY, c, lumaStep), SampleQuadY(NextY, c, lumaStep), Alpha);
        const float2 blendC = lerp(PrevUV.SampleLevel(LinearClamp, uv, 0), NextUV.SampleLevel(LinearClamp, uv, 0), Alpha);
        y = lerp(blendY, warpedY, g);
        chroma = lerp(blendC, warpedC, g);
    }
    else // mode 3
    {
        // Next's backward field points Next -> Prev; motion continues the other way:
        //   z - Alpha * B(z) = c   ->  z = c + Alpha * B(z)
        float conf;
        const float2 z = SolveWarp(FlowB, c, -Alpha, conf);
        const float g = saturate(conf * ConfGain);
        const float2 zz = lerp(c, z, g);
        y = SampleQuadY(NextY, zz, lumaStep);
        chroma = NextUV.SampleLevel(LinearClamp, zz / SrcSize, 0);
    }

    // Thin divider for the compare view.
    if (Compare != 0 && abs(outCenter.x - OutSize.x * 0.5) < 1.5)
    {
        y = 0.92;
        chroma = 0.5;
    }

    Store(id.xy, y, chroma);
}
