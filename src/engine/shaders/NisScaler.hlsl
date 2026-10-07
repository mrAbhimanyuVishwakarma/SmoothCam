// NVIDIA Image Scaling (NIS) - an alternative to FSR 1 for upscaling, run on
// the luma plane like EASU. NIS sharpens as part of scaling, so no RCAS pass
// follows it. It supports up to 2x per axis; bigger steps use FSR instead.
//
// Spatial only - one frame in, one frame out, no added latency.

#define NIS_HLSL 1
#define NIS_SCALER 1
#define NIS_HDR_MODE 0
#define NIS_NV12_SUPPORT 1
#define NIS_LUMA_ONLY 1 // see the SmoothCam note in third_party/nis/NIS_Scaler.h
#define NIS_CLAMP_OUTPUT 1
#define NIS_BLOCK_WIDTH 32
#define NIS_BLOCK_HEIGHT 24
#define NIS_THREAD_GROUP_SIZE 128

// Must match NISConfig in NIS_Config.h.
cbuffer cb : register(b0)
{
    float kDetectRatio;
    float kDetectThres;
    float kMinContrastRatio;
    float kRatioNorm;

    float kContrastBoost;
    float kEps;
    float kSharpStartY;
    float kSharpScaleY;

    float kSharpStrengthMin;
    float kSharpStrengthScale;
    float kSharpLimitMin;
    float kSharpLimitScale;

    float kScaleX;
    float kScaleY;

    float kDstNormX;
    float kDstNormY;
    float kSrcNormX;
    float kSrcNormY;

    uint kInputViewportOriginX;
    uint kInputViewportOriginY;
    uint kInputViewportWidth;
    uint kInputViewportHeight;

    uint kOutputViewportOriginX;
    uint kOutputViewportOriginY;
    uint kOutputViewportWidth;
    uint kOutputViewportHeight;

    float reserved0;
    float reserved1;
};

SamplerState samplerLinearClamp : register(s0);
Texture2D<float> in_texture_y : register(t0);
Texture2D coef_scaler : register(t1);
Texture2D coef_usm : register(t2);
RWTexture2D<unorm float> out_texture : register(u0);

// NIS stores a float4; only luma is kept, which is exactly what we want.
#pragma warning(disable : 3206) // implicit truncation of vector type
#include "NIS_Scaler.h"

[numthreads(NIS_THREAD_GROUP_SIZE, 1, 1)]
void main(uint3 blockIdx : SV_GroupID, uint3 threadIdx : SV_GroupThreadID)
{
    NVScaler(blockIdx.xy, threadIdx.x);
}
