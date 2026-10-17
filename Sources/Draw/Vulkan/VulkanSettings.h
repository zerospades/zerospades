/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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

#include <Core/SettingSet.h>

namespace spades {
	namespace draw {
		/**
		 * The settings the Vulkan renderer sizes or builds something from once: its
		 * render targets, images, pipelines and their specialization. Latched as
		 * GL's (`GLSettings`): read when the renderer is made, and the same for its
		 * whole life, so nothing built from one is ever met by a different value
		 * read later. Changing one takes a restart, as the setup screen says.
		 *
		 * Settings read afresh every frame (`r_scale`, `r_dlights`, ...) are not
		 * here, and stay live.
		 */
		class VulkanSettings : public SettingSet {
		public:
			// clang-format off
			TypedItemHandle<bool> r_bloom               { *this, "r_bloom", ItemFlags::Latch };
			TypedItemHandle<int> r_fogShadow            { *this, "r_fogShadow", ItemFlags::Latch };
			TypedItemHandle<bool> r_hdr                 { *this, "r_hdr", ItemFlags::Latch };
			TypedItemHandle<bool> r_highPrec            { *this, "r_highPrec", ItemFlags::Latch };
			TypedItemHandle<float> r_maxAnisotropy      { *this, "r_maxAnisotropy", ItemFlags::Latch };
			TypedItemHandle<bool> r_modelShadows        { *this, "r_modelShadows", ItemFlags::Latch };
			TypedItemHandle<bool> r_physicalLighting    { *this, "r_physicalLighting", ItemFlags::Latch };
			TypedItemHandle<int> r_radiosity            { *this, "r_radiosity", ItemFlags::Latch };
			TypedItemHandle<int> r_shadowMapSize        { *this, "r_shadowMapSize", ItemFlags::Latch };
			TypedItemHandle<int> r_softParticles        { *this, "r_softParticles", ItemFlags::Latch };
			TypedItemHandle<bool> r_srgb                { *this, "r_srgb", ItemFlags::Latch };
			TypedItemHandle<int> r_ssao                 { *this, "r_ssao", ItemFlags::Latch };
			TypedItemHandle<int> r_water                { *this, "r_water", ItemFlags::Latch };
			// clang-format on

			/** Whether the fog is the volumetric one lit by the radiosity (Fog2), as
			 * GL's `ShouldUseFogFilter2`: `r_fogShadow` 2 with radiosity on */
			bool UsesFog2() { return (int)r_fogShadow == 2 && (int)r_radiosity != 0; }
		};
	} // namespace draw
} // namespace spades
