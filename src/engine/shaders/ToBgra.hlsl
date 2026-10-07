// NV12 planes (limited range) -> BGRA8 for apps that ask for RGB32.
// The UAV is R8G8B8A8, so we write (B, G, R, A) into (r, g, b, a) to get
// B,G,R,A byte order in memory, which is what MFVideoFormat_RGB32 expects.

Texture2D<float> Y : register(t0);
Texture2D<float2> UV : register(t1);
RWTexture2D<unorm float4> Out : register(u0);

cbuffer Params : register(b0)
{
    uint2 Size;
    uint Bt709;
    uint Pad;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Size))
        return;

    const float y = (Y[id.xy] - 16.0 / 255.0) * (255.0 / 219.0);
    const float2 c = (UV[id.xy / 2] - 128.0 / 255.0) * (255.0 / 224.0);

    float3 rgb;
    if (Bt709)
        rgb = float3(y + 1.5748 * c.y, y - 0.1873 * c.x - 0.4681 * c.y, y + 1.8556 * c.x);
    else
        rgb = float3(y + 1.402 * c.y, y - 0.344136 * c.x - 0.714136 * c.y, y + 1.772 * c.x);

    rgb = saturate(rgb);
    Out[id.xy] = float4(rgb.b, rgb.g, rgb.r, 1.0);
}
