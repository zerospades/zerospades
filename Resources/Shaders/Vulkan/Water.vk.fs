#version 450

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

// =============================================================================
// Level 1 water (r_water 1). The "low" shader: cheap, no real reflection.
//
// Per-fragment:
//   1. Sample a single 2D wave texture three times at different scales (broad,
//      detail, rough) and blend into a tiny normal-vector wobble. wave.z is
//      fixed at 1/128, then normalize → the surface normal is essentially
//      pointing up with a small per-pixel wave bend.
//   2. Get the screen-space coord of this water vertex (origScrPos) and read
//      the pre-water depth there. envelope = how opaque the water layer is
//      vs. what's behind it (refraction). At sky-background pixels we force
//      envelope to 1 so the screenTexture lookup (which is a LINEAR sample
//      that can drag wall/sky colours across sharp edges) can't bleed.
//   3. waterColor: sample a per-cube blue gradient (mainTexture) with a
//      blur-direction integral; modulate by sunlight + ambient. Apply fog.
//   4. Mix refraction (screenTexture at origScrPos) with waterColor by
//      envelope.
//   5. Reflection at this level is *flat*: just skyColor * reflective * 0.6,
//      mixed in by reflective * 0.6 * att. No mirror texture, no real scene
//      reflection.
//   6. Specular: simple power-of-2 ramp on dot(reflectDir, sunDir) ^1024,
//      multiplied by 1000 → a tiny sharp sun glint when the angle lines up.
//   7. (sRGB sqrt at the end if the framebuffer is linear UNORM, otherwise
//      no-op because LINEAR_FRAMEBUFFER=1 here.)
// =============================================================================

// Vulkan uses SRGB framebuffer
#define LINEAR_FRAMEBUFFER 1

layout(location = 0) in vec3 v_fogDensity;
layout(location = 1) in vec3 v_screenPosition;
layout(location = 2) in vec3 v_viewPosition;
layout(location = 3) in vec3 v_worldPosition;

layout(binding = 0) uniform sampler2D screenTexture;
layout(binding = 1) uniform sampler2D depthTexture;
layout(binding = 2) uniform sampler2D mainTexture;
layout(binding = 3) uniform sampler2D waveTexture;

// Push constants for frequently updated per-frame data (112 bytes, within 128 byte limit)
layout(push_constant) uniform WaterPushConstants {
	vec4 fogColor; // xyz used
	vec4 skyColor; // xyz used
	vec2 zNearFar;
	vec2 _pad0;
	vec4 fovTan;
	vec4 waterPlane;
	vec4 viewOriginVector; // use .xyz
	vec2 displaceScale;
	vec2 _pad1;
	vec4 sunDirection;
} waterPC;

layout(location = 0) out vec4 fragColor;



// Sun lighting: matches OpenGL implementation
// Returns vec3(0.6) for full sunlight (shadows not implemented yet)
vec3 EvaluateSunLight() {
	return vec3(0.6); // Placeholder - should multiply by shadow visibility
}

// Ambient lighting: matches OpenGL implementation
vec3 EvaluateAmbientLight(float detailAmbientOcclusion) {
	return vec3(0.3, 0.3, 0.35) * detailAmbientOcclusion;
}

float decodeDepth(float w, float near, float far) {
	return far * near / mix(far, near, w);
}

float depthAt(vec2 pt) {
	float w = texture(depthTexture, pt).x;
	return decodeDepth(w, waterPC.zNearFar.x, waterPC.zNearFar.y);
}

void main() {
	vec3 worldPositionFromOrigin = v_worldPosition - waterPC.viewOriginVector.xyz;
	vec4 waveCoord = v_worldPosition.xyxy
		* vec4(vec2(0.08), vec2(0.15704))
		+ vec4(0.0, 0.0, 0.754, 0.1315);

	vec2 waveCoord2 = v_worldPosition.xy * 0.02344 + vec2(0.154, 0.7315);

	// evaluate waveform
	vec3 wave = texture(waveTexture, waveCoord.xy).xyz;
	wave = mix(vec3(-1.0), vec3(1.0), wave);
	wave.xy *= 0.08 / 200.0;

	// detail (Far Cry seems to use this technique)
	vec2 wave2 = texture(waveTexture, waveCoord.zw).xy;
	wave2 = mix(vec2(-1.0), vec2(1.0), wave2);
	wave2.xy *= 0.15704 / 200.0;
	wave.xy += wave2;

	// rough
	wave2 = texture(waveTexture, waveCoord2.xy).xy;
	wave2 = mix(vec2(-1.0), vec2(1.0), wave2);
	wave2.xy *= 0.02344 / 200.0;
	wave.xy += wave2;

	wave.z = (1.0 / 128.0);
	wave.xyz = normalize(wave.xyz);

	// Vertex shader already encoded ((gl.x+gl.w)*0.5, (gl.w-gl.y)*0.5, gl.w),
	// so the divide below yields a [0,1] tex coord directly.
	vec2 origScrPos = v_screenPosition.xy / v_screenPosition.z;

	// Sample depth at origScrPos. GL has a planeDistance branch here that
	// flickers on MoltenVK; the wave-displaced refraction lookup has been
	// removed for the same reason (it bled the wall edge into the water).
	float depth = depthAt(origScrPos);

	float envelope = clamp((depth + v_viewPosition.z), 0.0, 1.0);
	envelope = 1.0 - (1.0 - envelope) * (1.0 - envelope);

	// Vulkan-specific guard: at sky-background pixels (raw depth at the far
	// plane), force envelope to 1.0 so the screenTexture sample (which is
	// LINEAR-filtered and can blend wall/sky colors across the wall edge,
	// and whose decoded `depth + v_viewPosition.z` falls below 1 when the
	// water vertex is farther than the closest opaque sample) can't bleed
	// the cyan sky color through into the water. Tests at origScrPos —
	// no wave modulation, no wavy-line precision artifact.
	float rawDepth = texture(depthTexture, origScrPos).x;
	if (rawDepth >= 0.9999) {
		envelope = 1.0;
	}

	// water color
	// TODO: correct integral
	vec2 waterCoord = v_worldPosition.xy;
	vec2 integralCoord = floor(waterCoord) + 0.5;
	vec2 blurDir = (worldPositionFromOrigin.xy);
	blurDir /= max(length(blurDir), 1.0);
	vec2 blurDirSign = mix(vec2(-1.0), vec2(1.0), step(0.0, blurDir));
	vec2 startPos = (waterCoord - integralCoord) * blurDirSign;
	vec2 diffPos = blurDir * envelope * blurDirSign * 0.5 /*limit blur*/;
	vec2 subCoord = 1.0 - clamp((vec2(0.5) - startPos) / diffPos, 0.0, 1.0);
	vec2 sampCoord = integralCoord + subCoord * blurDirSign;
	vec3 waterColor = texture(mainTexture, sampCoord / 512.0).xyz;
	vec3 diffuseShading = EvaluateAmbientLight(1.0);
	vec3 sunlightForColor = EvaluateSunLight();
	waterColor *= sunlightForColor + diffuseShading;

	// underwater object color
	fragColor = texture(screenTexture, origScrPos);
#if !LINEAR_FRAMEBUFFER
	fragColor.xyz *= fragColor.xyz; // screen color to linear
#endif

	// apply fog color to water color now.
	// note that fog is already applied to underwater object.
	waterColor = mix(waterColor, waterPC.fogColor.xyz, v_fogDensity);

	// blend water color with the underwater object's color.
	fragColor.xyz = mix(fragColor.xyz, waterColor, envelope);

	// attenuation factor for addition blendings below
	vec3 att = 1.0 - v_fogDensity;

	// reflectivity
	vec3 sunlight = EvaluateSunLight();
	vec3 ongoing = normalize(worldPositionFromOrigin);
	float reflective = dot(wave, ongoing);
	reflective = clamp(1.0 - reflective, 0.0, 1.0);
	reflective *= reflective;
	reflective *= reflective;
	reflective += 0.03;

	// fresnel reflection of the sky
	fragColor.xyz = mix(fragColor.xyz,
		mix(waterPC.skyColor.xyz * reflective * 0.6,
			waterPC.fogColor.xyz, v_fogDensity), reflective * 0.6 * att);

	// specular reflection
	if (dot(sunlight, vec3(1.0)) > 0.0001) {
		vec3 refl = reflect(ongoing, wave);
		float spec = max(dot(refl, waterPC.sunDirection.xyz), 0.0);
		spec *= spec; // ^2
		spec *= spec; // ^4
		spec *= spec; // ^16
		spec *= spec; // ^32
		spec *= spec; // ^64
		spec *= spec; // ^128
		spec *= spec; // ^256
		spec *= spec; // ^512
		spec *= spec; // ^1024
		spec *= reflective;
		fragColor.xyz += sunlight * spec * 1000.0 * att;
	}

#if !LINEAR_FRAMEBUFFER
	fragColor.xyz = sqrt(fragColor.xyz);
#endif

	fragColor.w = 1.0;
}
