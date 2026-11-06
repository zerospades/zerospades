#pragma once
#include <cmath>

namespace spades { namespace android {
// In 980-unit HUD coordinates. Keep these in sync with HudLayout.java.
constexpr float PaletteX = 250.0f, PaletteY = 390.0f, PaletteSize = 165.0f;
constexpr float PaletteGap = 1.0f;

// The palette renderer distributes n cells and n-1 gaps across PaletteSize.
// Coordinates are relative to that rectangle. Invalid/outside input is ignored.
inline int PaletteIndex(float x, float y, int n) {
    if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 || x >= 1 || y >= 1 || n <= 0 || n > 16)
        return -1;
    const float cellStep = (PaletteSize + PaletteGap) / static_cast<float>(n);
    int col = static_cast<int>(x * PaletteSize / cellStep);
    int row = static_cast<int>(y * PaletteSize / cellStep);
    return row * n + col;
}
} }
