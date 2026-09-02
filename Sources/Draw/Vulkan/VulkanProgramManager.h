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

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanProgram;
		class VulkanShader;

		class VulkanProgramManager : public RefCountedObject {
			Handle<gui::SDLVulkanDevice> device;

			std::unordered_map<std::string, Handle<VulkanProgram>> programs;
			std::unordered_map<std::string, Handle<VulkanShader>> shaders;

			Handle<VulkanProgram> CreateProgram(const std::string& name);
			Handle<VulkanShader> CreateShader(const std::string& name);

		protected:
			~VulkanProgramManager();

		public:
			VulkanProgramManager(Handle<gui::SDLVulkanDevice> device);

			VulkanProgram* RegisterProgram(const std::string& name);
			VulkanShader* RegisterShader(const std::string& name);

			// Clear all cached programs and shaders
			void Clear();
		};

	} // namespace draw
} // namespace spades
