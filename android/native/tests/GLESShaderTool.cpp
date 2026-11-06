#include "../GLESShaderSource.h"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

using namespace spades::android;
static std::string Read(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("Cannot open " + path);
    return std::string(std::istreambuf_iterator<char>(f), {});
}
int main(int argc, char** argv) {
    if (argc != 10) {
        std::cerr << "usage: tool resource-dir manifest vertex|fragment hdr fog ssao radiosity model-shadows sparse-shadows\n";
        return 2;
    }
    try {
        GLESShaderSettings settings;
        settings.hdr = std::stoi(argv[4]) != 0;
        settings.fogShadow = std::stoi(argv[5]) != 0;
        settings.ssao = std::stoi(argv[6]) != 0;
        settings.radiosity = std::stoi(argv[7]);
        settings.modelShadows = std::stoi(argv[8]) != 0;
        settings.sparseShadowMaps = std::stoi(argv[9]) != 0;
        const std::string root = argv[1];
        const std::string stage = argv[3];
        if (stage != "vertex" && stage != "fragment") throw std::runtime_error("Invalid shader stage");
        std::vector<ShaderComponent> parts;
        for (const auto& name : ResolveGLESProgram(Read(argv[2]), settings)) {
            const std::string extension = stage == "vertex" ? ".vs" : ".fs";
            if (name.size() >= extension.size() && name.compare(name.size() - extension.size(), extension.size(), extension) == 0)
                parts.push_back({name, Read(root + "/" + name)});
        }
        std::cout << AssembleGLESShader(parts, stage == "fragment", settings);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
