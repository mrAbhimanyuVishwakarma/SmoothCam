// Lanczos-3 upscaling of the luma plane: the classic high-quality resampler
// (6x6 taps). Plain Lanczos rings around hard edges, so the result is pulled
// most of the way back into the range of the four nearest source pixels
// ("anti-ringing"), which keeps edges crisp without halos.

Texture2D<float> InputY : register(t0);
RWTexture2D<unorm float> OutputY : register(u0);

cbuffer Params : register(b0)
{
    uint2 InSize;
    uint2 OutSize;
};

static const float kPi = 3.14159265;
static const float kAntiRing = 0.8; // 0 = plain Lanczos, 1 = full clamp

float Lanczos3(float x)
{
    x = abs(x);
    if (x < 1e-4)
        return 1.0;
    if (x >= 3.0)
        return 0.0;
    const float px = kPi * x;
    return 3.0 * sin(px) * sin(px / 3.0) / (px * px);
}

float Tap(int2 p)
{
    return InputY.Load(int3(clamp(p, int2(0, 0), int2(InSize) - 1), 0));
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= OutSize))
        return;

    // Output pixel center in source pixel coordinates.
    const float2 src = (float2(id.xy) + 0.5) * float2(InSize) / float2(OutSize) - 0.5;
    const int2 base = int2(floor(src));
    const float2 f = src - float2(base);

    float wx[6], wy[6];
    float sx = 0, sy = 0;
    [unroll] for (int i = 0; i < 6; ++i)
    {
        wx[i] = Lanczos3(f.x - (i - 2));
        wy[i] = Lanczos3(f.y - (i - 2));
        sx += wx[i];
        sy += wy[i];
    }

    float sum = 0;
    [unroll] for (int j = 0; j < 6; ++j)
    {
        float row = 0;
        [unroll] for (int k = 0; k < 6; ++k)
            row += Tap(base + int2(k - 2, j - 2)) * wx[k];
        sum += row * wy[j];
    }
    sum /= sx * sy;

    const float a = Tap(base), b = Tap(base + int2(1, 0)), c = Tap(base + int2(0, 1)), d = Tap(base + int2(1, 1));
    const float lo = min(min(a, b), min(c, d));
    const float hi = max(max(a, b), max(c, d));
    OutputY[id.xy] = lerp(sum, clamp(sum, lo, hi), kAntiRing);
}
