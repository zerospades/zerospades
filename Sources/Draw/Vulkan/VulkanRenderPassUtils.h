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
#include <Core/Exception.h>

namespace spades {
	namespace draw {

		// Creates a simple single-attachment color render pass for post-processing filters.
		// This consolidates the common render pass creation pattern used across all filters.
		inline VkRenderPass CreateSimpleColorRenderPass(
			VkDevice device,
			VkFormat format,
			VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
			VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			VkImageLayout finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
			const VkSubpassDependency* pDependency = nullptr
		) {
			VkAttachmentDescription colorAttachment = {};
			colorAttachment.format = format;
			colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
			colorAttachment.loadOp = loadOp;
			colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			colorAttachment.initialLayout = initialLayout;
			colorAttachment.finalLayout = finalLayout;

			VkAttachmentReference colorAttachmentRef = {};
			colorAttachmentRef.attachment = 0;
			colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

			VkSubpassDescription subpass = {};
			subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
			subpass.colorAttachmentCount = 1;
			subpass.pColorAttachments = &colorAttachmentRef;

			VkRenderPassCreateInfo renderPassInfo = {};
			renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
			renderPassInfo.attachmentCount = 1;
			renderPassInfo.pAttachments = &colorAttachment;
			renderPassInfo.subpassCount = 1;
			renderPassInfo.pSubpasses = &subpass;

			// Post-process filters ping-pong between just two images, so the
			// image this pass writes is, at every filter boundary, the image the
			// *previous* pass was sampling. Callers declare the read-after-write
			// half of that ("the last pass wrote it, I read it"), but the
			// write-after-read half is equally real and was missing: nothing
			// stopped this pass's colour writes from starting before the
			// previous pass's fragment reads had finished, so a filter could
			// sample an image that was already being overwritten. The result is
			// intermittent, non-deterministic, full-screen corruption that
			// changes character whenever the number of active filters changes.
			//
			// An absent dependency does NOT mean "no constraint": the implicit
			// external dependency Vulkan supplies uses TOP_OF_PIPE with an empty
			// access mask and therefore guarantees nothing at all.
			VkSubpassDependency deps[3] = {};

			// [0] read-after-write: the caller's, or a sensible default.
			if (pDependency) {
				deps[0] = *pDependency;
			} else {
				deps[0].srcSubpass    = VK_SUBPASS_EXTERNAL;
				deps[0].dstSubpass    = 0;
				deps[0].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
				deps[0].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
				deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
				deps[0].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			}

			// [1] write-after-read AND write-after-write: this pass must not
			// begin writing the attachment (including the loadOp and the layout
			// transition, both of which count as writes) until earlier reads of
			// it are done AND earlier writes to it are available. The previous
			// owner of a ping-pong image wrote it with storeOp and a later pass
			// sampled it, so both directions apply. Transfer is included because
			// blits and copies move these images around too.
			deps[1].srcSubpass    = VK_SUBPASS_EXTERNAL;
			deps[1].dstSubpass    = 0;
			deps[1].srcStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			                        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
			                        VK_PIPELINE_STAGE_TRANSFER_BIT;
			deps[1].dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			deps[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT |
			                        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			                        VK_ACCESS_TRANSFER_READ_BIT |
			                        VK_ACCESS_TRANSFER_WRITE_BIT;
			deps[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
			                        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;

			// [2] OUTGOING: make this pass's writes visible to whoever samples the
			// result. Without an explicit 0 -> EXTERNAL dependency, Vulkan
			// supplies an implicit one using BOTTOM_OF_PIPE with an empty access
			// mask -- that orders execution but makes NO writes available, so a
			// later fragment shader may sample the attachment before the writes
			// land. This bites hardest on the MSAA depth-resolve pass, whose only
			// purpose is to produce an image that depth-reading post filters
			// (depth of field, the lens-flare scanner) then sample.
			deps[2].srcSubpass    = 0;
			deps[2].dstSubpass    = VK_SUBPASS_EXTERNAL;
			deps[2].srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			deps[2].dstStageMask  = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			                        VK_PIPELINE_STAGE_TRANSFER_BIT;
			deps[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			deps[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT |
			                        VK_ACCESS_TRANSFER_READ_BIT;

			renderPassInfo.dependencyCount = 3;
			renderPassInfo.pDependencies = deps;

			VkRenderPass renderPass;
			if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
				SPRaise("Failed to create render pass");
			}

			return renderPass;
		}

	} // namespace draw
} // namespace spades
