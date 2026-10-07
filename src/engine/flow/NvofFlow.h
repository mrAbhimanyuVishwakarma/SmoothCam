#pragma once

#include "Flow.h"

#include <memory>

namespace smoothcam
{
// NVIDIA Optical Flow (dedicated hardware on Turing and newer GPUs).
// Only motion vectors come from NVIDIA; confidence and compositing are ours.
class NvofFlow final : public FlowEstimator
{
public:
    NvofFlow(const GpuDevice& gpu, int workLevel);
    ~NvofFlow() override;

    // Fails cleanly on non-NVIDIA GPUs, missing driver support, or when the
    // D3D adapter isn't the CUDA device.
    HRESULT Init();

    const wchar_t* Name() const override { return L"NVIDIA Optical Flow"; }
    HRESULT Estimate(const LumaPyramid& a, const LumaPyramid& b, const Texture* previousForward,
                     RawFlow& out) override;
    int WorkLevel() const override { return m_level; }

    // Sign convention of the hardware vectors, verified by the unit tests.
    // +1: vector points from input frame to reference frame.
    static constexpr float kSign = 1.0f;

private:
    struct Impl;
    const GpuDevice& m_gpu;
    int m_level;
    std::unique_ptr<Impl> m_impl;
};

// True if the driver exposes NVIDIA Optical Flow on this machine (cheap check, no session).
bool IsNvofRuntimePresent();
} // namespace smoothcam
