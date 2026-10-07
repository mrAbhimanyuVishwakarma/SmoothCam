// Bilinear resize of the NV12 chroma plane (used alongside EASU on luma).

Texture2D<float2> Src : register(t0);
RWTexture2D<unorm float2> Dst : register(u0);
SamplerState LinearClamp : register(s0);

cbuffer Params : register(b0)
{
    uint2 DstSize;
    uint2 Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= DstSize))
        return;
    Dst[id.xy] = Src.SampleLevel(LinearClamp, (float2(id.xy) + 0.5) / float2(DstSize), 0);
}
