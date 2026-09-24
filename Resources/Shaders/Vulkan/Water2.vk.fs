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
// Level 2 water (r_water 2). Diff vs. level 1 (Water.vk.fs):
//
//   • Wave normal comes from a sampler2DArray (3 layers) instead of a single
//     2D texture — each octave reads its own layer with its own amplitude/
//     frequency. The constants are also smaller (wave.xy *= 0.08*0.4 etc.),
//     and wave.z = (1/128)/4 → a more pronounced surface bend.
//
//   • Real mirror reflection: samples a mirrorTexture (the scene rendered
//     from the reflected camera) at a wave-displaced screen position scrPos2.
//     scrPos2 uses |disp.y| forced negative so the reflection is "pulled
//     down" along the wave — the classic stretched water-reflection look.
//     The mirror sample uses textureLod with a distance-based lodBias so
//     far-water reflections are softer than near-water ones.
//
//   • Reflection mix uses a Fresnel-ish curve (reflective^4 then attenuated
//     toward orig_reflective*0.6 at grazing horizon LOD) instead of the
//     fixed 0.6 of level 1.
//
//   • Specular swap: GGX distribution with Kelemen geometric shadowing and
//     reflective-modulated Fresnel, capped at 50 to avoid eyeball-melting
//     spikes. Much more physically motivated than level 1's pow ramp.
//
// Everything else (refraction lookup at origScrPos, envelope guard at sky
// pixels, waterColor integral, fog, sqrt for sRGB) is identical to level 1.
// =============================================================================

// Vulkan uses SRGB framebuffer
#define LINEAR_FRAMEBUFFER 1

layout(location = 0) in vec3 v_fogDensity;
layout(location = 1) in vec3 v_screenPosition;
layout(location = 2) in vec3 v_viewPosition;
layout(location = 3) in vec3 v_worldPosition;
layout(location = 4) in vec2 v_worldPositionOriginal;

layout(binding = 0) uniform sampler2D screenTexture;
layout(binding = 1) uniform sampler2D depthTexture;
layout(binding = 2) uniform sampler2D mainTexture;
layout(binding = 8) uniform sampler2DArray waveTextureArray;
layout(binding = 6) uniform sampler2D mirrorTexture;

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



// GGX distribution function for specular
float GGXDistribution(float m, float dotHalf) {
	float m2 = m * m;
	float d = dotHalf * dotHalf * (m2 - 1.0) + 1.0;
	return m2 / (3.14159265 * d * d);
}

vec3 EvaluateSunLight() {
	return vec3(0.6); // Placeholder - should multiply by shadow visibility
}

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
	vec4 waveCoord = v_worldPositionOriginal.xyxy
		* vec4(vec2(0.08), vec2(0.15704))
		+ vec4(0., 0., 0.754, 0.1315);

	vec2 waveCoord2 = v_worldPositionOriginal.xy * 0.02344 + vec2(0.154, 0.7315);

	// evaluate waveform (normal vector)
	vec3 wave = texture(waveTextureArray, vec3(waveCoord.xy, 0.0)).xyz;
	wave = mix(vec3(-0.0025), vec3(0.0025), wave);
	wave.xy *= 0.08 * 0.4;

	// detail
	vec2 wave2 = texture(waveTextureArray, vec3(waveCoord.zw, 1.0)).xy;
	wave2 = mix(vec2(-0.0025), vec2(0.0025), wave2);
	wave2.xy *= 0.15704 * 0.2;
	wave.xy += wave2;

	// rough
	wave2 = texture(waveTextureArray, vec3(waveCoord2.xy, 2.0)).xy;
	wave2 = mix(vec2(-0.0025), vec2(0.0025), wave2);
	wave2.xy *= 0.02344 * 2.5;
	wave.xy += wave2;

	wave.z = (1.0 / 128.0) / 4.0; // (negated normal vector!)
	wave.xyz = normalize(wave.xyz);

	vec2 origScrPos = v_screenPosition.xy / v_screenPosition.z;

	// disp and scale are still used below for the mirror-reflection sample
	// (scrPos2). The refraction lookup is undisplaced: wave-displacing it
	// here bled wall/sky pixels into the water on MoltenVK.
	float scale = 1.0 / v_viewPosition.z;
	vec2 disp = wave.xy * 0.1;

	float depth = depthAt(origScrPos);

	float envelope = clamp((depth + v_viewPosition.z), 0.0, 1.0);
	envelope = 1.0 - (1.0 - envelope) * (1.0 - envelope);

	// Vulkan-specific guard: at sky-background pixels (raw depth at the far
	// plane), force envelope to 1.0 so the screenTexture sample can't bleed
	// the cyan sky color through into the water. Tested at origScrPos so
	// there's no wave modulation in the test result.
	float rawDepth = texture(depthTexture, origScrPos).x;
	if (rawDepth >= 0.9999) {
		envelope = 1.0;
	}

	vec3 sunlight = EvaluateSunLight();

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
	waterColor *= sunlight + EvaluateAmbientLight(1.0);

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

	/* ------- Reflection -------- */

	vec3 ongoing = normalize(worldPositionFromOrigin);

    // bluring for far surface
	float lodBias = 1.0 / ongoing.z;
	float dispScaleByLod = min(1.0, ongoing.z * 0.5);
    lodBias = log2(lodBias);
    lodBias = clamp(lodBias, 0.0, 2.0);

	// compute reflection color
	vec2 scrPos2 = origScrPos;
	disp.y = -abs(disp.y * 3.0);
	scrPos2 -= disp * scale * waterPC.displaceScale * 15.0;

	vec3 refl = textureLod(mirrorTexture, scrPos2, lodBias).xyz;
#if !LINEAR_FRAMEBUFFER
	refl *= refl; // linearize
#endif

	// reflectivity
	float dotNV = dot(wave, ongoing);
	float reflective = clamp(1.0 - dotNV, 0.0, 1.0);
    float orig_reflective = reflective;
	reflective *= reflective;
	reflective *= reflective;
    reflective = mix(reflective, orig_reflective * 0.6, clamp(lodBias * 0.13 - 0.13, 0.0, 1.0));
	//reflective += 0.03;

	// reflection
#if USE_VOLUMETRIC_FOG
	// it's actually impossible for water reflection to cope with volumetric fog.
	// fade the water reflection so that we don't see sharp boundary of water
	refl *= att;
#endif
	fragColor.xyz = mix(fragColor.xyz, refl, reflective * att);

	/* ------- Specular Reflection -------- */

	// specular reflection
	if (dot(sunlight, vec3(1.0)) > 0.0001) {
		// can't use CockTorrance here -- CockTorrance's fresenel term
		// is hard-coded for higher roughness values
		vec3 lightVec = -waterPC.sunDirection.xyz * 1.4142135;
		vec3 halfVec = lightVec + ongoing;
		halfVec = (dot(halfVec, halfVec) < 0.00000000001)
			? vec3(1.0, 0.0, 0.0) : normalize(halfVec);

		float dotNL = max(dot(wave, lightVec), 0.0);
		float dotNH = max(dot(wave, halfVec), 0.00001);

		// distribution
		float m = 0.002 + 0.0003 / (abs(ongoing.z) + 0.0006); // roughness
		float spec = GGXDistribution(m, dotNH);

		// fresnel
		spec *= reflective;

		// geometric shadowing (Kelemen)
		float visibility = (dotNL*dotNV) / (dotNH*dotNH);
		spec *= max(0.0, visibility);

		// limit brightness (flickering specular reflection might cause seizure to some people)
		spec = min(spec, 50.0);

		fragColor.xyz += sunlight * spec * att;
	}

#if !LINEAR_FRAMEBUFFER
	fragColor.xyz = sqrt(fragColor.xyz);
#endif

	fragColor.w = 1.0;
}
