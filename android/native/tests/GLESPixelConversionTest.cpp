#include "../GLESPixelConversion.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>
using namespace spades::android;
int main() {
    const std::uint8_t input[] = {1, 2, 3, 4, 10, 20, 30, 40};
    auto bytes = ConvertBgraToRgba(input, 1, 2, 1, BgraPixelType::Bytes);
    assert(bytes == std::vector<std::uint8_t>({3, 2, 1, 4, 30, 20, 10, 40}));
    // BGRA packed reversed: low B, middle G, high R, high 2 bits A.
    std::uint32_t packed = (3U << 30) | (777U << 20) | (44U << 10) | 9U;
    auto converted = ConvertBgraToRgba(&packed, 1, 1, 1, BgraPixelType::Packed2101010Rev);
    std::uint32_t rgba;
    std::memcpy(&rgba, converted.data(), 4);
    assert(rgba == ((3U << 30) | (9U << 20) | (44U << 10) | 777U));
    assert(ConvertBgraToRgba(nullptr, 10, 10, 1, BgraPixelType::Bytes).empty());
    bool rejected = false;
    try { (void)ConvertBgraToRgba(input, -1, 1, 1, BgraPixelType::Bytes); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { (void)ConvertBgraToRgba(input, std::numeric_limits<int>::max(),
                                  std::numeric_limits<int>::max(), 8, BgraPixelType::Bytes); }
    catch (const std::length_error&) { rejected = true; }
    assert(rejected);
}
