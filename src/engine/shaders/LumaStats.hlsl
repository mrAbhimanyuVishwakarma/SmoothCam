// Average brightness of a (small) luma image, smoothed over time. Feeds the
// low-light boost. One thread group of 16x16 reduces the whole image.

Texture2D<float> Luma : register(t0);
RWTexture2D<float> Stats : register(u0); // 1x1, R32_FLOAT

cbuffer Params : register(b0)
{
    uint2 Size;
    float Rate; // EMA weight of the new value
    float Pad;
};

groupshared float g_partial[256];

[numthreads(16, 16, 1)]
void main(uint3 tid : SV_GroupThreadID)
{
    const uint index = tid.y * 16 + tid.x;
    float sum = 0;
    for (uint y = tid.y; y < Size.y; y += 16)
        for (uint x = tid.x; x < Size.x; x += 16)
            sum += Luma[uint2(x, y)];
    g_partial[index] = sum;
    GroupMemoryBarrierWithGroupSync();

    [unroll]
    for (uint stride = 128; stride > 0; stride >>= 1)
    {
        if (index < stride)
            g_partial[index] += g_partial[index + stride];
        GroupMemoryBarrierWithGroupSync();
    }

    if (index == 0)
    {
        const float mean = g_partial[0] / float(Size.x * Size.y);
        const float old = Stats[uint2(0, 0)];
        Stats[uint2(0, 0)] = old <= 0 ? mean : lerp(old, mean, Rate);
    }
}
