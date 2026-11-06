#include "GLESShaderSource.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>
#include <stdexcept>

namespace spades { namespace android {
namespace {
std::string Trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
}
void AddPair(std::vector<std::string>& out, const std::string& stem) {
    out.push_back("Shaders/OpenGL/" + stem + ".fs");
    out.push_back("Shaders/OpenGL/" + stem + ".vs");
}
bool Declaration(const std::string& line) {
    // GLSL's uniform/attribute/varying qualifiers only occur at global scope
    // in the bundled shaders. A multi-line declaration is left for the driver
    // to reject if it cannot be combined, rather than removing unrelated code.
    for (const char* prefix : {"uniform", "attribute", "varying"}) {
        const std::string p(prefix);
        if (line.size() > p.size() && line.compare(0, p.size(), p) == 0 &&
            std::isspace(static_cast<unsigned char>(line[p.size()])) &&
            line.find(';') != std::string::npos) return true;
    }
    return false;
}
}

std::vector<std::string> ResolveGLESProgram(const std::string& manifest,
                                            const GLESShaderSettings& settings) {
    std::vector<std::string> files;
    std::istringstream input(manifest);
    std::string line;
    while (std::getline(input, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "*shadow*" || line == "*shadow-lite*" || line == "*shadow-variance*") {
            files.push_back(line == "*shadow-lite*" ? "Shaders/OpenGL/Shadow/CommonNoSSAO.fs"
                                                    : "Shaders/OpenGL/Shadow/Common.fs");
            files.push_back("Shaders/OpenGL/Shadow/Common.vs");
            AddPair(files, line == "*shadow-variance*" ? "Shadow/MapVariance" : "Shadow/Map");
            AddPair(files, !settings.modelShadows ? "Shadow/ModelNull" :
                           settings.sparseShadowMaps ? "Shadow/ModelSparse" : "Shadow/Model");
            AddPair(files, settings.radiosity ? "Shadow/MapRadiosity" : "Shadow/MapRadiosityNull");
        } else if (line == "*dlight*") {
            AddPair(files, "DynamicLight/Common");
            AddPair(files, "DynamicLight/MapNull");
        } else if (line == "*shadowmap*") {
            AddPair(files, "ShadowMap/Common");
            AddPair(files, "ShadowMap/Basic");
        } else if (line[0] == '*') {
            throw std::invalid_argument("Unknown special shader: " + line);
        } else {
            files.push_back(line);
        }
    }
    return files;
}

std::string AssembleGLESShader(const std::vector<ShaderComponent>& components,
                               bool fragment, const GLESShaderSettings& settings) {
    if (components.empty()) throw std::invalid_argument("Empty GLES shader stage");
    std::ostringstream out;
    out << "#version 300 es\n"
        << "precision highp float;\nprecision highp int;\n"
        << "precision highp sampler2DArray;\nprecision highp sampler3D;\n"
        << "precision highp sampler2DShadow;\n"
        << "#define USE_HDR " << int(settings.hdr) << "\n"
        << "#define LINEAR_FRAMEBUFFER " << int(settings.hdr) << "\n"
        << "#define USE_VOLUMETRIC_FOG " << int(settings.fogShadow) << "\n"
        << "#define USE_SSAO " << int(settings.ssao) << "\n"
        << "#define USE_RADIOSITY " << std::max(0, std::min(2, settings.radiosity)) << "\n"
        << "#define texture2D texture\n#define texture3D texture\n"
        << "#define texture2DArray texture\n#define texture2DArrayLod textureLod\n"
        << "#define texture2DProj textureProj\n#define shadow2D(s,c) vec4(texture(s,c))\n";
    if (fragment) out << "#define varying in\nout vec4 zsFragColor;\n#define gl_FragColor zsFragColor\n";
    else out << "#define varying out\n#define attribute in\n";

    std::map<std::string, unsigned> declarations;
    for (const auto& component : components) {
        out << "\n// Component: " << component.name << "\n";
        std::istringstream input(component.source);
        std::string line;
        while (std::getline(input, line)) {
            const auto trimmed = Trim(line);
            if (trimmed.compare(0, 8, "#version") == 0)
                throw std::invalid_argument(component.name + ": unexpected #version");
            // Texture arrays are part of ES 3.0. The desktop extension name is
            // not necessarily present on an ES driver.
            if (trimmed == "#extension GL_EXT_texture_array : require") continue;
            if (trimmed.compare(0, 10, "#extension") == 0)
                throw std::invalid_argument(component.name + ": unsupported #extension: " + trimmed);
            if (Declaration(trimmed)) {
                auto inserted = declarations.emplace(trimmed, static_cast<unsigned>(declarations.size()));
                auto id = inserted.first->second;
                out << "#ifndef ZS_GLES_DECL_" << id << "\n#define ZS_GLES_DECL_" << id << "\n"
                    << line << "\n#endif\n";
            } else {
                out << line << '\n';
            }
        }
    }
    return out.str();
}

} }
