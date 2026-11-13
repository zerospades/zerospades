/*
 Copyright (c) 2013 Fran6nd
 Vulkan port (c) 2024

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

#include <memory>

#include <Gui/SDLVulkanDevice.h>
#include "VulkanRenderer.h"
#include "VulkanModelManager.h"
#include "VulkanOptimizedVoxelModel.h"
#include <Core/Debug.h>
#include <Core/IStream.h>
#include <Core/Settings.h>
#include <Core/VoxelModel.h>
#include <Core/VoxelModelLoader.h>

namespace spades {
	namespace draw {
		VulkanModelManager::VulkanModelManager(VulkanRenderer& r) : renderer{r} {
			SPADES_MARK_FUNCTION();
		}

		VulkanModelManager::~VulkanModelManager() {
			SPADES_MARK_FUNCTION();
		}

		Handle<client::IModel> VulkanModelManager::RegisterModel(const char* name) {
			SPADES_MARK_FUNCTION();

			auto it = models.find(std::string(name));
			if (it == models.end()) {
				Handle<client::IModel> m = CreateModel(name);
				models[name] = m;
				return m;
			}
			return it->second;
		}

		Handle<client::IModel> VulkanModelManager::CreateModel(const char* name) {
			SPADES_MARK_FUNCTION();

			auto voxelModel = VoxelModelLoader::Load(name);
			return renderer.CreateModel(*voxelModel);
		}

		void VulkanModelManager::ClearCache() {
			models.clear();
		}
	} // namespace draw
} // namespace spades
