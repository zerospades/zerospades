/*
 Copyright (c) 2013 Fran6nd

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "VulkanProgramManager.h"
#include "VulkanProgram.h"
#include "VulkanShader.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/FileManager.h>
#include <Core/Stopwatch.h>
#include <Core/TMPUtils.h>
#include <Core/Math.h>

namespace spades {
	namespace draw {

		VulkanProgramManager::VulkanProgramManager(Handle<gui::SDLVulkanDevice> dev)
		: device(std::move(dev)) {
			SPADES_MARK_FUNCTION();
		}

		VulkanProgramManager::~VulkanProgramManager() {
			SPADES_MARK_FUNCTION();
		}

		VulkanProgram* VulkanProgramManager::RegisterProgram(const std::string& name) {
			SPADES_MARK_FUNCTION();

			auto it = programs.find(name);
			if (it == programs.end()) {
				auto program = CreateProgram(name);
				VulkanProgram* programPtr = program.GetPointerOrNull();
				programs[name] = std::move(program);
				return programPtr;
			} else {
				return it->second.GetPointerOrNull();
			}
		}

		VulkanShader* VulkanProgramManager::RegisterShader(const std::string& name) {
			SPADES_MARK_FUNCTION();

			auto it = shaders.find(name);
			if (it == shaders.end()) {
				auto shader = CreateShader(name);
				VulkanShader* shaderPtr = shader.GetPointerOrNull();
				shaders[name] = std::move(shader);
				return shaderPtr;
			} else {
				return it->second.GetPointerOrNull();
			}
		}

		Handle<VulkanProgram> VulkanProgramManager::CreateProgram(const std::string& name) {
			SPADES_MARK_FUNCTION();

			SPLog("Loading Vulkan program '%s'", name.c_str());
			Stopwatch sw;

			std::string text = FileManager::ReadAllBytes(name.c_str());
			std::vector<std::string> lines = SplitIntoLines(text);

			auto program = Handle<VulkanProgram>::New(device, name);

			// Parse .program file (same format as OpenGL)
			// Format:
			// Line 1-N: shader file paths (vertex shader first, fragment shader second, etc.)
			// Lines starting with '*' are special directives (e.g., *shadow*)
			for (size_t i = 0; i < lines.size(); i++) {
				std::string line = TrimSpaces(lines[i]);

				if (line.empty() || line[0] == '#') {
					continue; // Skip empty lines and comments
				}

				if (line[0] == '*') {
					// Special directive (e.g., *shadow*)
					// For now, we'll ignore these
					continue;
				}

				// It's a shader file path
				VulkanShader* shader = RegisterShader(line);
				if (shader && shader->IsCompiled()) {
					program->AttachShader(shader);
				}
			}

			// Link the program
			program->Link();

			SPLog("Loaded Vulkan program '%s' in %.3f ms", name.c_str(), sw.GetTime() * 1000.0);
			return program;
		}

		Handle<VulkanShader> VulkanProgramManager::CreateShader(const std::string& name) {
			SPADES_MARK_FUNCTION();

			SPLog("Loading Vulkan shader '%s'", name.c_str());

			// All shaders must be pre-compiled SPIR-V (.spv files)
			if (name.find(".spv") == std::string::npos) {
				SPRaise("Shader '%s' is not a pre-compiled SPIR-V file. "
				        "All Vulkan shaders must be compiled to .spv at build time.", name.c_str());
			}

			// Determine shader type from extension
			VulkanShader::Type type;
			if (name.find(".vert.spv") != std::string::npos || name.find(".vk.vs") != std::string::npos) {
				type = VulkanShader::VertexShader;
			} else if (name.find(".frag.spv") != std::string::npos || name.find(".vk.fs") != std::string::npos) {
				type = VulkanShader::FragmentShader;
			} else if (name.find(".geom.spv") != std::string::npos || name.find(".vk.gs") != std::string::npos) {
				type = VulkanShader::GeometryShader;
			} else if (name.find(".comp.spv") != std::string::npos || name.find(".vk.cs") != std::string::npos) {
				type = VulkanShader::ComputeShader;
			} else {
				SPRaise("Unknown shader type for '%s'", name.c_str());
			}

			auto shader = Handle<VulkanShader>::New(device, type, name);

			// Load pre-compiled SPIR-V binary
			std::string source = FileManager::ReadAllBytes(name.c_str());
			std::vector<uint32_t> spirvCode(source.size() / sizeof(uint32_t));
			memcpy(spirvCode.data(), source.data(), source.size());
			shader->LoadSPIRV(spirvCode);

			return shader;
		}

		void VulkanProgramManager::Clear() {
			programs.clear();
			shaders.clear();
			SPLog("Cleared Vulkan program manager cache");
		}

	} // namespace draw
} // namespace spades
