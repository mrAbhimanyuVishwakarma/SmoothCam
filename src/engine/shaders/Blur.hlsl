// Separable Gaussian blur (one direction per dispatch) on the quarter-size
// background image.

Texture2D<float4> In : register(t0);
RWTexture2D<float4> Out : register(u0);

cbuffer Params : register(b0)
{
    uint2 Size;
    int2 Direction; // (1,0) or (0,1)
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Size))
        return;
    // 13 taps, sigma ~3 texels (= 12 output pixels).
    static const float weights[7] = {0.1370, 0.1290, 0.1080, 0.0800, 0.0530, 0.0310, 0.0160};
    const int2 lim = int2(Size) - 1;
    float4 sum = In[id.xy] * weights[0];
    [unroll]
    for (int i = 1; i < 7; ++i)
    {
        sum += In[clamp(int2(id.xy) + Direction * i, 0, lim)] * weights[i];
        sum += In[clamp(int2(id.xy) - Direction * i, 0, lim)] * weights[i];
    }
    Out[id.xy] = sum / (weights[0] + 2.0 * (weights[1] + weights[2] + weights[3] + weights[4] + weights[5] + weights[6]));
}
