#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

namespace spades { namespace android {

// Desktop accepts BGRA uploads; core GLES 3 only guarantees RGBA. The game
// uses tightly packed BGRA bytes and BGRA packed 2:10:10:10 reversed values.
// The caller verifies pixel-store state and absence of a pixel unpack buffer.
enum class BgraPixelType { Bytes, Packed2101010Rev };
inline std::vector<std::uint8_t> ConvertBgraToRgba(const void* input, int width, int height,
                                                   int depth, BgraPixelType type) {
    if (width < 0 || height < 0 || depth < 0)
        throw std::invalid_argument("negative pixel dimensions");
    std::size_t count = static_cast<std::size_t>(width);
    for (int dimension : {height, depth}) {
        if (dimension && count > std::numeric_limits<std::size_t>::max() /
                                   static_cast<std::size_t>(dimension))
            throw std::length_error("pixel dimensions overflow");
        count *= static_cast<std::size_t>(dimension);
    }
    if (count > std::numeric_limits<std::size_t>::max() / 4)
        throw std::length_error("pixel byte count overflow");
    if (!input || count == 0) return {};
    std::vector<std::uint8_t> output(count * 4);
    const auto* src = static_cast<const std::uint8_t*>(input);
    if (type == BgraPixelType::Bytes) {
        for (std::size_t i = 0; i < count; ++i) {
            output[4*i] = src[4*i+2]; output[4*i+1] = src[4*i+1];
            output[4*i+2] = src[4*i]; output[4*i+3] = src[4*i+3];
        }
    } else {
        for (std::size_t i = 0; i < count; ++i) {
            std::uint32_t pixel;
            std::memcpy(&pixel, src + 4*i, sizeof(pixel));
            pixel = (pixel & 0xC00FFC00U) | ((pixel & 0x000003FFU) << 20) |
                    ((pixel >> 20) & 0x000003FFU);
            std::memcpy(output.data() + 4*i, &pixel, sizeof(pixel));
        }
    }
    return output;
}

} }
