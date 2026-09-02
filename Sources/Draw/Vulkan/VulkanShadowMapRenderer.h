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

#include <vulkan/vulkan.h>
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
	namespace gui {
		class SDLVulkanDevice;
	}

	namespace draw {
		class VulkanRenderer;
		class VulkanImage;
		class VulkanBuffer;

		class VulkanShadowMapRenderer {
		public:
			enum { NumSlices = 3 };

		private:
			VulkanRenderer& renderer;
			Handle<gui::SDLVulkanDevice> device;

			int textureSize;

			Handle<VulkanImage> shadowMapImages[NumSlices];
			VkFramebuffer framebuffers[NumSlices];
			VkRenderPass renderPass;

			Matrix4 matrix;
			Matrix4 matrices[NumSlices];
			OBB3 obb;
			float vpWidth, vpHeight;

			VkPipelineLayout pipelineLayout;
			VkDescriptorSetLayout descriptorSetLayout;
			VkPipeline pipeline;
			VkDescriptorPool descriptorPool;
			VkDescriptorSet descriptorSets[NumSlices];
			Handle<VulkanBuffer> uniformBuffers[NumSlices];

			// Sampling side: one descriptor set (cascade-matrix UBO + the NumSlices
			// depth maps) that the lit shaders bind to read model shadows.
			VkDescriptorSetLayout samplingSetLayout;
			VkDescriptorPool samplingPool;
			VkDescriptorSet samplingDescriptorSet;
			Handle<VulkanBuffer> samplingUniformBuffer;
			VkSampler samplingSampler;

			// Returns false when the cascade fit is degenerate; `matrix` is then
			// left untouched and must not be used. Mirrors
			// GLBasicShadowMapRenderer::BuildMatrix.
			bool BuildMatrix(float near, float far);
			void CreateRenderPass();
			void CreateFramebuffers();
			void CreatePipeline();
			void CreateDescriptorSets();
			void CreateSamplingResources();
			void DestroyResources();

		public:
			VulkanShadowMapRenderer(VulkanRenderer&);
			~VulkanShadowMapRenderer();

			void Render(VkCommandBuffer commandBuffer);
			// Mark the sampling UBO disabled for frames the cascade isn't rendered.
			void SetSamplingDisabled();

			bool Cull(const AABB3&);
			bool SphereCull(const Vector3& center, float rad);

			const Matrix4& GetMatrix() const { return matrix; }
			const Matrix4* GetMatrices() const { return matrices; }
			VulkanImage* GetShadowMapImage(int slice) { return shadowMapImages[slice].GetPointerOrNull(); }
			VkPipelineLayout GetPipelineLayout() const { return pipelineLayout; }

			// Bound by the map/model sunlight pipelines to sample model shadows.
			VkDescriptorSetLayout GetSamplingSetLayout() const { return samplingSetLayout; }
			VkDescriptorSet GetSamplingDescriptorSet() const { return samplingDescriptorSet; }
		};
	}
}
