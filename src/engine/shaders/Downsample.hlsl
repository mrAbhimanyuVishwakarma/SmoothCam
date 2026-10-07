// 2x2 box downsample of a single-channel image. Used to build the luma pyramid;
// the first level reads the NV12 luma plane directly.

Texture2D<float> Src : register(t0);
RWTexture2D<unorm float> Dst : register(u0);

cbuffer Params : register(b0)
{
    uint2 DstSize;
    uint2 SrcSize;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= DstSize))
        return;

    const int2 maxPos = int2(SrcSize) - 1;
    const int2 s = int2(id.xy) * 2;
    const float sum = Src[min(s, maxPos)] + Src[min(s + int2(1, 0), maxPos)] +
                      Src[min(s + int2(0, 1), maxPos)] + Src[min(s + int2(1, 1), maxPos)];
    Dst[id.xy] = sum * 0.25;
}
