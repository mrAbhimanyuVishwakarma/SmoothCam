#pragma once

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <vector>

namespace smoothcam
{
// Reads any image WIC understands as top-down 32-bit BGRA. COM must be initialized.
HRESULT ReadImageBgra(const std::filesystem::path& path, std::vector<uint8_t>& pixels, uint32_t& width,
                      uint32_t& height);

// Writes a top-down 32-bit BGRA image as PNG (via WIC). COM must be initialized.
HRESULT WritePngBgra(const std::filesystem::path& path, const uint8_t* pixels, uint32_t width, uint32_t height,
                     uint32_t stride);
} // namespace smoothcam
