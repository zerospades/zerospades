#pragma once

#include <string>
#include <vector>

namespace spades { namespace android {

// Mirrors the renderer settings that affect manifest expansion and the shader
// preamble. Kept independent of GL so the real game shaders can be validated
// offline as well as on device.
struct GLESShaderSettings {
    bool hdr = false;
    bool fogShadow = false;
    bool ssao = false;
    int radiosity = 0;
    bool modelShadows = false;
    bool sparseShadowMaps = false;
};

struct ShaderComponent {
    std::string name;
    std::string source;
};

// Expand a .program manifest into the same components used by the desktop
// shader helpers. Unknown special entries are errors, never silently dropped.
std::vector<std::string> ResolveGLESProgram(const std::string& manifest,
                                            const GLESShaderSettings& settings);

// Assemble exactly one GLSL ES 3.00 source per shader stage. Identical global
// declarations are guarded by the GLSL preprocessor, preserving conditional
// declarations. Conflicting declarations remain visible to the compiler.
std::string AssembleGLESShader(const std::vector<ShaderComponent>& components,
                               bool fragment, const GLESShaderSettings& settings);

} }
