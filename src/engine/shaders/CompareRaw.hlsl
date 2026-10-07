// Compare view: paints the left half of the finished frame straight from the
// camera frame as delivered - plainly stretched, no smoothing, upscaling or
// effects - and draws a thin divider down the middle. The right half keeps
// SmoothCam's result.

Texture2D<float> RawY : register(t0);
Texture2D<float2> RawUV : register(t1);
RWTexture2D<unorm float> OutY : register(u0);
RWTexture2D<unorm float2> OutUV : register(u1);
SamplerState LinearClamp : register(s0);

cbuffer Params : register(b0)
{
    uint2 OutSize;
    float2 CropOffset; // region of the camera frame that fills the output
    float2 CropScale;
    uint2 Pad;
};

static const float kDividerY = 0.92;

// One thread per 2x2 quad (one chroma sample).
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint2 q = id.xy * 2;
    if (q.y >= OutSize.y)
        return;
    const float mid = OutSize.x * 0.5;

    bool anyWritten = false;
    bool divider = false;
    [unroll] for (uint dy = 0; dy < 2; ++dy)
    {
        [unroll] for (uint dx = 0; dx < 2; ++dx)
        {
            const uint2 p = q + uint2(dx, dy);
            if (any(p >= OutSize))
                continue;
            const float x = p.x + 0.5;
            if (abs(x - mid) < 1.5)
            {
                OutY[p] = kDividerY;
                divider = true;
            }
            else if (x < mid)
            {
                const float2 uv = CropOffset + (float2(p) + 0.5) / float2(OutSize) * CropScale;
                OutY[p] = RawY.SampleLevel(LinearClamp, uv, 0);
                anyWritten = true;
            }
        }
    }

    const uint2 c = id.xy;
    if (divider)
        OutUV[c] = float2(0.5, 0.5);
    else if (anyWritten)
    {
        const float2 uv = CropOffset + (float2(q) + 1.0) / float2(OutSize) * CropScale;
        OutUV[c] = RawUV.SampleLevel(LinearClamp, uv, 0);
    }
}
