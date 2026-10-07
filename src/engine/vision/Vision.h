#pragma once

#include "Clock.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace smoothcam
{
// A face, normalized to the source frame (0..1).
struct FaceBox
{
    float x = 0;
    float y = 0;
    float w = 0;
    float h = 0;
};

// Runs the slower "understanding" work off the render thread:
//   * face detection with Windows' built-in FaceDetector (for auto-framing)
//   * person segmentation with a small ONNX model through Windows ML (for the
//     background blur)
// Both ship with Windows, so nothing extra is installed. Inputs are small CPU
// images; if the worker is busy, older inputs are simply replaced by newer ones.
class VisionWorker
{
public:
    VisionWorker();
    ~VisionWorker();
    VisionWorker(const VisionWorker&) = delete;
    VisionWorker& operator=(const VisionWorker&) = delete;

    void Configure(bool faces, bool segmentation, const std::filesystem::path& modelPath);

    void SubmitLuma(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride);
    void SubmitRgba(const uint8_t* pixels, uint32_t width, uint32_t height, uint32_t stride);

    // Newest person mask (8-bit probability, temporally smoothed), if there is a new one.
    bool TakeMask(std::vector<uint8_t>& mask, uint32_t& width, uint32_t& height);
    // Latest detected faces and when they were detected.
    std::vector<FaceBox> Faces(Hns& when) const;
    // Empty when everything works; otherwise a short user-facing explanation.
    std::wstring Problem() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace smoothcam
