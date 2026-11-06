#include "../TouchPalette.h"
#include <cassert>
#include <limits>
#include <initializer_list>
using namespace spades::android;
int main() {
    for (int n : {8, 16}) {
        const float step = (PaletteSize + PaletteGap) / n;
        const float cell = step - PaletteGap;
        for (int row = 0; row < n; ++row) {
            for (int col = 0; col < n; ++col) {
                float x = (col * step + cell * .5f) / PaletteSize;
                float y = (row * step + cell * .5f) / PaletteSize;
                assert(PaletteIndex(x, y, n) == row * n + col);
            }
        }
        assert(PaletteIndex(0, 0, n) == 0);
        assert(PaletteIndex(.999f, .999f, n) == n*n - 1);
        assert(PaletteIndex(1, 0, n) == -1);
        assert(PaletteIndex(-.01f, 0, n) == -1);
        assert(PaletteIndex(std::numeric_limits<float>::quiet_NaN(), 0, n) == -1);
    }
    assert(PaletteIndex(0, 0, 0) == -1);
    assert(PaletteIndex(0, 0, 17) == -1);
}
