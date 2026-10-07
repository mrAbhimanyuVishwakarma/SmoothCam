// Converts NVIDIA Optical Flow output (S10.5 fixed point, in pixels of the
// image the hardware saw) into float vectors in full-resolution source pixels.

Texture2D<int2> Src : register(t0);
RWTexture2D<float2> Dst : register(u0);

cbuffer Params : register(b0)
{
    uint2 Size;
    float Scale; // (1/32) * full-res / hardware-input-res, sign folded in
    float Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Size))
        return;
    Dst[id.xy] = float2(Src[id.xy]) * Scale;
}
