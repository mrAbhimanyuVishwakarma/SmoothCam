// Turns a raw vector field into a refined field with a confidence value.
//
// Confidence combines two checks:
//   * forward/backward consistency - following a vector and then the opposite
//     field's vector should land back where we started. Occlusions and bad
//     matches fail this.
//   * photometric error - the mean absolute luma difference of the matched
//     patch. Tolerant enough for webcam sensor noise.

#include "Common.hlsli"

Texture2D<float2> Field : register(t0);    // A -> B, on A's grid, full-res pixels
Texture2D<float2> Opposite : register(t1); // B -> A, on B's grid, full-res pixels
Texture2D<float> LumaA : register(t2);     // any pyramid level of A
Texture2D<float> LumaB : register(t3);     // same level of B
RWTexture2D<float4> Out : register(u0);

cbuffer Params : register(b0)
{
    uint2 Grid;        // size of Field / Out
    float2 FrameSize;  // full-res source size in pixels
    float2 CellSize;   // full-res pixels per grid cell
    float SigmaBase;   // allowed round-trip error, pixels
    float SigmaRel;    // ... plus this fraction of the vector length
    float MadScale;    // photometric tolerance
    float3 Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Grid))
        return;

    const float2 v = Field[id.xy];
    const float2 p = (float2(id.xy) + 0.5) * CellSize;
    const float2 q = p + v;

    const float2 back = Opposite.SampleLevel(LinearClamp, q / FrameSize, 0);
    const float err = length(v + back);
    const float sigma = SigmaBase + SigmaRel * length(v);
    float conf = exp(-(err * err) / (2.0 * sigma * sigma));

    // Photometric check on a 4x4 lattice inside the cell.
    float mad = 0;
    [unroll]
    for (int y = 0; y < 4; ++y)
    {
        [unroll]
        for (int x = 0; x < 4; ++x)
        {
            const float2 o = (float2(x, y) + 0.5) / 4.0 - 0.5;
            const float2 pa = p + o * CellSize;
            const float a = LumaA.SampleLevel(LinearClamp, pa / FrameSize, 0);
            const float b = LumaB.SampleLevel(LinearClamp, (pa + v) / FrameSize, 0);
            mad += abs(a - b);
        }
    }
    mad *= 1.0 / 16.0;
    const float m = mad / MadScale;
    conf *= exp(-m * m);

    // Content that moves out of frame can't be matched reliably.
    if (any(q < 0) || any(q > FrameSize))
        conf *= 0.3;

    Out[id.xy] = float4(v, conf, 0);
}
