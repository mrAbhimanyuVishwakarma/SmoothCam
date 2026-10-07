// Shared helpers for SmoothCam compute shaders.
//
// Conventions
//   * Flow vectors are in *source pixels* of the full-resolution frame unless
//     a shader says otherwise.
//   * A forward field is defined on the grid of the earlier frame and points to
//     where that content is in the later frame. A backward field is the reverse.
//   * Final (refined) fields are float4: xy = vector, z = confidence 0..1.

#ifndef SMOOTHCAM_COMMON_HLSLI
#define SMOOTHCAM_COMMON_HLSLI

SamplerState LinearClamp : register(s0);

#define BLOCK 8

float2 PixelToUv(float2 pixel, float2 size)
{
    return pixel / size;
}

#endif
