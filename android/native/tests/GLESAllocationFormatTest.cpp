#include "../GLESAllocationFormat.h"
#include <cassert>

int main() {
    using GL = spades::draw::IGLDevice;
    struct Case { GL::Enum internal, format, type; };
    const Case cases[] = {
        {GL::RGBA8, GL::RGBA, GL::UnsignedByte},
        {GL::SRGB8Alpha, GL::RGBA, GL::UnsignedByte},
        {GL::RGB8, GL::RGB, GL::UnsignedByte},
        {GL::SRGB8, GL::RGB, GL::UnsignedByte},
        {GL::RGB10A2, GL::RGBA, GL::UnsignedInt2101010Rev},
        {GL::RGBA16F, GL::RGBA, GL::FloatType},
        {GL::RGB16F, GL::RGB, GL::FloatType},
        {GL::R16F, GL::Red, GL::FloatType},
        {GL::RGB5A1, GL::RGBA, GL::UnsignedShort5551},
    };
    for (const auto& test : cases) {
        auto format = GL::DepthStencil;
        auto type = GL::UnsignedInt248;
        assert(spades::android::GLESStorageOnlyFormat(test.internal, format, type));
        assert(format == test.format && type == test.type);
    }
    auto format = GL::DepthStencil;
    auto type = GL::UnsignedInt248;
    assert(!spades::android::GLESStorageOnlyFormat(GL::Depth24Stencil8, format, type));
    assert(format == GL::DepthStencil && type == GL::UnsignedInt248);
}
