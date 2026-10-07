// First step of the background blur: shrink the frame by 4x into one RGBA16F
// texture holding (Y, U, V). Blurring at quarter size is 16x cheaper and a
// large blur looks the same.

Texture2D<float> InY : register(t0);
Texture2D<float2> InUV : register(t1);
RWTexture2D<float4> Out : register(u0);

cbuffer Params : register(b0)
{
    uint2 OutSize;
    uint2 InSize;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= OutSize))
        return;
    const int2 lim = int2(InSize) - 1;
    const int2 base = int2(id.xy) * 4;
    float y = 0;
    [unroll]
    for (int dy = 0; dy < 4; ++dy)
    {
        [unroll]
        for (int dx = 0; dx < 4; ++dx)
            y += InY[min(base + int2(dx, dy), lim)];
    }
    const int2 climb = int2((InSize + 1) / 2) - 1;
    const int2 cbase = int2(id.xy) * 2;
    const float2 uv = (InUV[min(cbase, climb)] + InUV[min(cbase + int2(1, 0), climb)] +
                       InUV[min(cbase + int2(0, 1), climb)] + InUV[min(cbase + int2(1, 1), climb)]) * 0.25;
    Out[id.xy] = float4(y / 16.0, uv, 1);
}
