#pragma once
#include <Draw/OpenGL/IGLDevice.h>

namespace spades { namespace android {

// Desktop GL permits more format/type combinations than GLES 3. These are
// canonical combinations for storage-only sized color texture allocations.
// Only use this when pixels == nullptr AND no pixel unpack buffer is bound:
// with a PBO, nullptr means byte offset zero and points to real upload data.
inline bool GLESStorageOnlyFormat(draw::IGLDevice::Enum internalFormat,
                                  draw::IGLDevice::Enum& format,
                                  draw::IGLDevice::Enum& type) {
    using GL = draw::IGLDevice;
    switch (internalFormat) {
        case GL::RGBA8:
        case GL::SRGB8Alpha:
            format = GL::RGBA; type = GL::UnsignedByte; return true;
        case GL::RGB8:
        case GL::SRGB8:
            format = GL::RGB; type = GL::UnsignedByte; return true;
        case GL::RGB10A2:
            format = GL::RGBA; type = GL::UnsignedInt2101010Rev; return true;
        case GL::RGBA16F:
            format = GL::RGBA; type = GL::FloatType; return true;
        case GL::RGB16F:
            format = GL::RGB; type = GL::FloatType; return true;
        case GL::R16F:
            format = GL::Red; type = GL::FloatType; return true;
        case GL::RGB5A1:
            format = GL::RGBA; type = GL::UnsignedShort5551; return true;
        default:
            return false;
    }
}

} }
