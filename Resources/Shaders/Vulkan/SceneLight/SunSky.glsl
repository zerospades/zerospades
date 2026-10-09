/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

// The sun's and the sky's light as the frame has them: `VulkanSceneLights::SunSky`.
// At night there is no sun, and the sky's light, the fog and the sky go dark with
// the daylight; the dynamic lights are not affected. The includer defines
// `SCENE_LIGHT_SET`.

layout(set = SCENE_LIGHT_SET, binding = 6, std140) uniform SceneSunSky {
	// The factor the sun's light is drawn with, in [0, 1]: at 0 it lights and
	// shadows nothing, and the map bounces none of it
	float sunlight;
	// The factor the sky's light is drawn with, in [0, 1]: 1 is full daylight
	float daylight;
	// The sky's light falling on surfaces, linear, in full daylight: the colour the
	// solid passes fade to
	vec3 skyLight;
	// The ambient light the radiosity adds, linear, in full daylight: half the fog
	// colour, kept bright enough to see by under a black sky
	vec3 ambientLight;
} sceneSunSky;
