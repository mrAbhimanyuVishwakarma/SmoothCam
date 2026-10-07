// 3x3 vector median filter. Picks the neighbour vector with the smallest total
// distance to the other eight - removes isolated outliers without blurring
// motion boundaries the way averaging would. Also rescales the vectors (used to
// convert from pyramid-level pixels to full-resolution pixels).

Texture2D<float2> Src : register(t0);
RWTexture2D<float2> Dst : register(u0);

cbuffer Params : register(b0)
{
    uint2 Size;
    float Scale;
    float Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Size))
        return;

    const int2 lim = int2(Size) - 1;
    float2 v[9];
    [unroll]
    for (int i = 0; i < 9; ++i)
        v[i] = Src[clamp(int2(id.xy) + int2(i % 3 - 1, i / 3 - 1), 0, lim)];

    float bestSum = 1e30;
    float2 best = v[4];
    [unroll]
    for (int a = 0; a < 9; ++a)
    {
        float sum = 0;
        [unroll]
        for (int b = 0; b < 9; ++b)
        {
            const float2 d = v[a] - v[b];
            sum += abs(d.x) + abs(d.y);
        }
        if (sum < bestSum)
        {
            bestSum = sum;
            best = v[a];
        }
    }
    Dst[id.xy] = best * Scale;
}
