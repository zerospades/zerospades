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

#version 450
#extension GL_GOOGLE_include_directive : require

// A soft sprite lit as the volume it stands for (`r_softParticles` 2), as GL's
// SoftLitSprite.fs: the sun through the map's and the models' shadows, the sky's
// light, and the dynamic lights the vertices scattered, on its colour, then
// fogged and faded where the scene's depth cuts into it like a soft sprite.

// Selects the radiosity's ambient light, as the map's shaders do (r_radiosity).
layout(constant_id = 0) const int USE_RADIOSITY = 0;

#define SCENE_LIGHT_SET 2
#include "SceneLight/SunSky.glsl"
#include "SceneLight/MapLight.glsl"
#include "SceneLight/ModelShadow.glsl"
#include "SpriteView.glsl"

// The scene's depth, which a soft sprite fades into
layout(set = SPRITE_SET, binding = 2) uniform sampler2D depthTexture;

layout(location = 0) in vec4 color;
layout(location = 1) in vec4 texCoord;
layout(location = 2) in vec4 fogDensity;
layout(location = 3) in vec4 depthRange;
layout(location = 4) in vec3 emission;
layout(location = 5) in vec3 normal;
layout(location = 6) in vec3 dynamicLight;
layout(location = 7) in vec3 shadowCoord;
layout(location = 8) in vec3 aoCoord;
layout(location = 9) in vec3 radiosityTextureCoord;
layout(location = 10) in vec3 modelShadowCoord0;
layout(location = 11) in vec3 modelShadowCoord1;
layout(location = 12) in vec3 modelShadowCoord2;
layout(location = 13) in float shadowViewDepth;

layout(location = 0) out vec4 fragColor;

float decodeDepth(float w, float near, float far) {
	return 1.0 / mix(far, near, w);
}

float depthAt(vec2 pt) {
	float w = texture(depthTexture, pt).x;
	return decodeDepth(w, spriteView.nearFar.x, spriteView.nearFar.y);
}

/** The sky's light reaching the volume, facing `nrm` (GL EvaluateAmbientLight(1)). */
vec3 SkyLight(vec3 nrm) {
	if (USE_RADIOSITY != 0) {
		float aoFactor = MapAmbientShadow(aoCoord);
		float amb = mix(sqrt(aoFactor), min(aoFactor, 1.0), 0.5) * (0.8 - nrm.z * 0.2);
		return MapRadiosity(radiosityTextureCoord, nrm) +
		       amb * (sceneSunSky.ambientLight * sceneSunSky.daylight);
	}
	float hemisphere = 1.0 - nrm.z * 0.2;
	return mix(sceneSunSky.skyLight, vec3(1.0), 0.5) * (0.5 * hemisphere * sceneSunSky.daylight);
}

void main() {
	float depth = depthAt(texCoord.zw);
	if (depth < depthRange.y)
		discard;

	vec3 nrm = normalize(normal);

	// The sun, wrapped around the volume as GL does
	float sunVisibility =
	  MapShadowVisibility(shadowCoord) *
	  ModelShadowVisibility(modelShadowCoord0, modelShadowCoord1, modelShadowCoord2,
	                        shadowViewDepth);
	vec3 diffuse = vec3(0.6 * sceneSunSky.sunlight * sunVisibility) *
	               (dot(nrm, normalize(spriteView.sunDirection.xyz)) * 0.4 + 0.6);
	diffuse += dynamicLight;
	diffuse += SkyLight(nrm);

	// GL brings the light down by a square root before it colours the sprite, in
	// every framebuffer: kept, so the sprites come out as bright as there.
	diffuse = sqrt(max(diffuse, 0.0));

	vec4 computedColor = color;
	computedColor.xyz *= diffuse;
	computedColor.xyz += emission;

	fragColor = texture(mainTexture, texCoord.xy);

	// Linearize the sampled texel; see the note in Sprite.frag.
	fragColor.xyz *= fragColor.xyz;

	fragColor.xyz *= fragColor.w; // premultiplied alpha
	fragColor *= computedColor;

	vec4 fogColorP = vec4(spriteView.fogColorDistance.xyz, 1.0);
	fogColorP *= fragColor.w; // premultiplied alpha
	fragColor = mix(fragColor, fogColorP, fogDensity);

	// Soft particle fade at geometry edges
	float soft = depth * depthRange.z + depthRange.x;
	soft = smoothstep(0.0, 1.0, soft);
	fragColor *= soft;

	fragColor = max(fragColor, 0.0);

	if (dot(fragColor, vec4(1.0)) < 0.002)
		discard;
}
