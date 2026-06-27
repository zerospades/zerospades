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

#include "VulkanPipelineCache.h"
#include <Gui/SDLVulkanDevice.h>
#include <Core/Debug.h>
#include <Core/FileManager.h>
#include <Core/IStream.h>
#include <fstream>
#include <vector>

namespace spades {
	namespace draw {

		VulkanPipelineCache::VulkanPipelineCache(Handle<gui::SDLVulkanDevice> device)
			: device(device),
			  pipelineCache(VK_NULL_HANDLE) {

			cachePath = "PipelineCache.bin";

			VkPipelineCacheCreateInfo createInfo = {};
			createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

			std::vector<uint8_t> cacheData;
			if (LoadFromDisk()) {
				try {
					auto stream = FileManager::OpenForReading(cachePath.c_str());
					size_t size = static_cast<size_t>(stream->GetLength());
					cacheData.resize(size);
					stream->Read(cacheData.data(), size);

					if (ValidateCacheHeader(cacheData)) {
						createInfo.initialDataSize = cacheData.size();
						createInfo.pInitialData = cacheData.data();
						SPLog("Loading pipeline cache from disk (%zu bytes)", cacheData.size());
					} else {
						SPLog("Pipeline cache validation failed, starting fresh");
						cacheData.clear();
					}
				} catch (const std::exception& e) {
					SPLog("Failed to read pipeline cache: %s", e.what());
					cacheData.clear();
				}
			}

			VkResult result = vkCreatePipelineCache(device->GetDevice(), &createInfo, nullptr, &pipelineCache);
			if (result != VK_SUCCESS) {
				SPLog("Failed to create pipeline cache (error code: %d), continuing without cache", result);
				pipelineCache = VK_NULL_HANDLE;
			} else {
				SPLog("Pipeline cache created successfully");
			}
		}

		VulkanPipelineCache::~VulkanPipelineCache() {
			SaveToDisk();

			if (pipelineCache != VK_NULL_HANDLE) {
				vkDestroyPipelineCache(device->GetDevice(), pipelineCache, nullptr);
			}
		}

		bool VulkanPipelineCache::LoadFromDisk() {
			try {
				return FileManager::FileExists(cachePath.c_str());
			} catch (...) {
				return false;
			}
		}

		bool VulkanPipelineCache::ValidateCacheHeader(const std::vector<uint8_t>& data) {
			if (data.size() < sizeof(VkPipelineCacheHeaderVersionOne)) {
				return false;
			}

			const VkPipelineCacheHeaderVersionOne* header =
				reinterpret_cast<const VkPipelineCacheHeaderVersionOne*>(data.data());

			if (header->headerSize < sizeof(VkPipelineCacheHeaderVersionOne)) {
				return false;
			}

			if (header->headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) {
				SPLog("Pipeline cache header version mismatch");
				return false;
			}

			VkPhysicalDeviceProperties props;
			vkGetPhysicalDeviceProperties(device->GetPhysicalDevice(), &props);

			if (header->vendorID != props.vendorID) {
				SPLog("Pipeline cache vendor ID mismatch");
				return false;
			}

			if (header->deviceID != props.deviceID) {
				SPLog("Pipeline cache device ID mismatch");
				return false;
			}

			if (memcmp(header->pipelineCacheUUID, props.pipelineCacheUUID, VK_UUID_SIZE) != 0) {
				SPLog("Pipeline cache UUID mismatch");
				return false;
			}

			return true;
		}

		void VulkanPipelineCache::SaveToDisk() {
			if (pipelineCache == VK_NULL_HANDLE) {
				return;
			}

			size_t cacheSize = 0;
			VkResult result = vkGetPipelineCacheData(device->GetDevice(), pipelineCache, &cacheSize, nullptr);
			if (result != VK_SUCCESS || cacheSize == 0) {
				SPLog("Failed to get pipeline cache size");
				return;
			}

			std::vector<uint8_t> cacheData(cacheSize);
			result = vkGetPipelineCacheData(device->GetDevice(), pipelineCache, &cacheSize, cacheData.data());
			if (result != VK_SUCCESS) {
				SPLog("Failed to get pipeline cache data");
				return;
			}

			try {
				auto stream = FileManager::OpenForWriting(cachePath.c_str());
				stream->Write(cacheData.data(), cacheSize);
				SPLog("Pipeline cache saved to disk (%zu bytes)", cacheSize);
			} catch (const std::exception& e) {
				SPLog("Failed to save pipeline cache: %s", e.what());
			}
		}

	} // namespace draw
} // namespace spades
