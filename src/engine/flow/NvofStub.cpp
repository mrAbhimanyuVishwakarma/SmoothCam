// Used when SmoothCam is built with SMOOTHCAM_WITH_NVOF=OFF: the NVIDIA path
// reports itself as unavailable and the engine uses its own compute flow.

#include "NvofFlow.h"

namespace smoothcam
{
struct NvofFlow::Impl
{
};

bool IsNvofRuntimePresent()
{
    return false;
}

NvofFlow::NvofFlow(const GpuDevice& gpu, int workLevel) : m_gpu(gpu), m_level(workLevel) {}
NvofFlow::~NvofFlow() = default;

HRESULT NvofFlow::Init()
{
    return E_NOTIMPL;
}

HRESULT NvofFlow::Estimate(const LumaPyramid&, const LumaPyramid&, const Texture*, RawFlow&)
{
    return E_NOTIMPL;
}
} // namespace smoothcam
