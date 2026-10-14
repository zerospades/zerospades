/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

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

#include <Core/Debug.h>

#include "GLSettings.h"

// FIXME: not yet implemented in Vulkan
DEFINE_SPADES_SETTING(r_blitFramebuffer, "1");
// FIXME: not yet implemented in Vulkan
DEFINE_SPADES_SETTING(r_debugTiming, "0");
// FIXME: not yet implemented in Vulkan
DEFINE_SPADES_SETTING(r_debugTimingOutputScreen, "1");
// FIXME: not yet implemented in Vulkan
DEFINE_SPADES_SETTING(r_debugTimingOutputLog, "0");
// FIXME: not yet implemented in Vulkan
DEFINE_SPADES_SETTING(r_debugTimingAverage, "1");
// FIXME: not yet implemented in Vulkan
DEFINE_SPADES_SETTING(r_debugTimingGPUTime, "1");
// FIXME: not yet implemented in Vulkan
DEFINE_SPADES_SETTING(r_debugTimingOutputBarScale, "2");
// FIXME: not yet implemented in Vulkan
DEFINE_SPADES_SETTING(r_debugTimingFlush, "0");
// FIXME: not yet implemented in Vulkan
DEFINE_SPADES_SETTING(r_debugTimingFillGap, "0");
// FIXME: not yet implemented in Vulkan
DEFINE_SPADES_SETTING(r_sparseShadowMaps, "1");
// FIXME: not yet implemented in Vulkan
DEFINE_SPADES_SETTING(r_srgb2D, "1");

namespace spades {
	namespace draw {
		GLSettings::GLSettings() {}

		void GLSettings::ValidateSettings() {
			if (this->r_fogShadow.operator int() == 2 && !this->ShouldUseFogFilter2())
				SPLog("`r_fogShadow 2` is ignored because `r_radiosity` is disabled");
		}

		bool GLSettings::ShouldUseFogFilter2() {
			return this->r_fogShadow.operator int() == 2 && !!this->r_radiosity.operator int();
		}
	} // namespace draw
} // namespace spades