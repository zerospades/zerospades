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
// Level 3 water (r_water 3). Diff vs. level 2 (Water2.vk.fs):
//
//   • Refraction is screen-space ray-marched. From the water vertex, compute
//     a refracted view direction (Snell's law, IOR 1.5) and march along it in
//     screen space against the scene's depth texture in 16 steps with a per-
//     fragment dither. The hit produces refractTargetSS — the screen coord
//     where the refracted ray actually exits the water — instead of just
//     sampling at origScrPos like level 1/2.
//
//   • envelope is distance-based: how far the refracted ray travelled before
//     hitting something, not the simple depth+viewZ. Underwater detail dims
//     with travel distance rather than scene depth alone.
//
//   • Reflection also gets a ray-march. First pass samples the mirror image
//     (mirrorTexture / mirrorDepthTexture); if planeDistance > 0 says the
//     mirror sample is actually a valid above-water hit, use it. Otherwise
//     fall back to an SSR ray-march on the regular scene to find what the
//     reflected ray would have seen. This gives reflections of things outside
//     the mirror render's frustum (e.g. nearby geometry behind the camera).
//
//   • reflectedSky tracks whether the chosen reflection sample landed at the
//     far plane (depth > 0.99999). The specular sun term only fires when the
//     reflection is sky — looking at an actual reflected wall, you shouldn't
//     also get a sun glint through it.
//
//   • Wave parameters retuned for the bigger surface: half-frequency wave
//     coords (0.04 vs 0.08, 0.08704 vs 0.15704, 0.00844 vs 0.02344) with
//     correspondingly larger amplitudes, and wave.z = (1/256)/4 → smaller-Z,
//     more horizontal bend → sharper distortion on the refracted ray.
//
// Everything else (mirror-reflection tap with wave-displaced scrPos, Fresnel
// LOD rolloff, GGX specular shape, envelope guard at sky pixels, sqrt for
// sRGB) is structurally the same as level 2.
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
layout(binding = 7) uniform sampler2D mirrorDepthTexture;

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

// Matrices UBO for viewMatrix (needed for SSR calculations)
layout(std140, binding = 5) uniform WaterMatricesUBO {
	mat4 projectionViewModelMatrix;
	mat4 modelMatrix;
	mat4 viewModelMatrix;
	mat4 viewMatrix;
	mat4 projectionViewMatrix;
	vec4 viewOriginVector;
	float fogDistance;
	vec3 _pad2;
} waterMat;

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

float encodeDepth(float z, float near, float far) {
	// FN/(F(1-w) + Nw) = z
	// FN = z(w(N-F) + F)
	// FN = zw(N-F) + Fz
	// w = F(N - z) / z(N - F)
	return far * (near + z) / (z * (far - near));
}

float depthAt(vec2 pt) {
	float w = texture(depthTexture, pt).x;
	return decodeDepth(w, waterPC.zNearFar.x, waterPC.zNearFar.y);
}

void main() {
	vec3 worldPositionFromOrigin = v_worldPosition - waterPC.viewOriginVector.xyz;
	vec4 waveCoord = v_worldPositionOriginal.xyxy * vec4(vec2(0.04), vec2(0.08704)) + vec4(0.0, 0.0, 0.754, 0.1315);
	vec2 waveCoord2 = v_worldPositionOriginal.xy * 0.00844 + vec2(0.154, 0.7315);

	// evaluate waveform (normal vector)
	vec3 wave = texture(waveTextureArray, vec3(waveCoord.xy, 0.0)).xyz;
	wave = mix(vec3(-0.0025), vec3(0.0025), wave);
	wave.xy *= 0.04 * 1.8;

	// detail
	vec2 wave2 = texture(waveTextureArray, vec3(waveCoord.zw, 1.0)).xy;
	wave2 = mix(vec2(-0.0025), vec2(0.0025), wave2);
	wave2.xy *= 0.08704 * 1.2;
	wave.xy += wave2;

	// rough
	wave2 = texture(waveTextureArray, vec3(waveCoord2.xy, 2.0)).xy;
	wave2 = mix(vec2(-0.0025), vec2(0.0025), wave2);
	wave2.xy *= 0.00844 * 2.5;
	wave.xy += wave2;

	wave.z = (1.0 / 256.0) / 4.0; // (negated normal vector!)
	wave.xyz = normalize(wave.xyz);

	vec2 origScrPos = v_screenPosition.xy / v_screenPosition.z;

	/* ------- Refraction -------- */

	// Compute the line segment for refraction ray tracing
	vec3 normalVS = (waterMat.viewMatrix * vec4(-wave, 0.0)).xyz;
	vec3 refractedVS = refract(normalize(v_viewPosition.xyz), normalVS, 1.0 / 1.5);
	vec3 refractTargetVS = v_viewPosition + refractedVS;
	if (refractTargetVS.z > -0.001) {
		refractTargetVS = mix(v_viewPosition, refractedVS,
			(-0.001 - v_viewPosition.z) / (refractedVS.z - v_viewPosition.z));
	}

	vec3 refractTargetNDC = vec3(
		refractTargetVS.xy / refractTargetVS.z / waterPC.fovTan.xy,
		encodeDepth(refractTargetVS.z, waterPC.zNearFar.x, waterPC.zNearFar.y)
	);

	// disp and scale used below for the reflection-tap displacement (scrPos2
	// in the reflection block); the SSR refraction loop drives refractTargetSS
	// from refractTargetNDC, no wave displacement on the lookup itself.
	float scale = 1.0 / v_viewPosition.z;
	vec2 disp = wave.xy * 0.1;

	vec2 refractTargetSS = refractTargetNDC.xy * vec2(-0.5, -0.5) + 0.5;

	// Screen-space ray tracing
	float origDepth = gl_FragCoord.z;
	vec2 targetScrPos = refractTargetSS;
	float targetDepth = refractTargetNDC.z;
	float depth;
	float dither = fract(dot(fract(gl_FragCoord.xy * 0.5), vec2(0.5)));
	for (float i = dither / 16.0; i <= 1.0; i += 1.0 / 16.0) {
		float rayDepth = mix(origDepth, targetDepth, i);
		refractTargetSS = mix(origScrPos, targetScrPos, i);
		depth = texture(depthTexture, refractTargetSS).x;
		if (depth < rayDepth && // ray intersects the object
			depth > rayDepth - 0.1) { // (perhaps ray's actually going behind the object!)
			i = max(0.0, i - 1.0 / 16.0);
			refractTargetSS = mix(origScrPos, targetScrPos, i);
			depth = texture(depthTexture, refractTargetSS).x;
			break;
		}
	}

	// convert to linear Z
	depth = decodeDepth(depth, waterPC.zNearFar.x, waterPC.zNearFar.y);

	// GL has a `if (planeDistance < 0) { reset to origScrPos }` here. On
	// MoltenVK the planeDistance=0 boundary is wave-modulated and shows as
	// a thin animated wavy line inside the rendered water. Dropping the
	// branch — same fix as Water/Water2.vk.fs — and using the SSR-marched
	// refractTargetSS/depth as-is removes the discontinuity entirely.
	//
	// fovTan carries OpenGL's screen-Y sign while refractTargetSS is a Vulkan
	// texture UV (Y inverted), so the reconstruction needs the GL-convention
	// UV (1 - y) to recover the correct view-space Y — otherwise the envelope
	// distance below is computed against a Y-mirrored point (matches the
	// reflection plane-test fix; texture sampling still uses the real UV).
	vec2 refractReconSS = vec2(refractTargetSS.x, 1.0 - refractTargetSS.y);
	vec3 sampledViewCoord = vec3(mix(waterPC.fovTan.zw, waterPC.fovTan.xy, refractReconSS), 1.0) * -depth;

	float envelope = min(distance(v_viewPosition * vec3(-1.0, 1.0, 1.0), sampledViewCoord) * 0.8, 1.0);
	envelope = 1.0 - (1.0 - envelope) * (1.0 - envelope);

	// Vulkan-specific guard: at sky-background pixels (raw depth at the far
	// plane), force envelope to 1.0 so the screenTexture sample can't bleed
	// the cyan sky color through. Same approach as Water/Water2.vk.fs.
	float rawDepth = texture(depthTexture, origScrPos).x;
	if (rawDepth >= 0.9999) {
		envelope = 1.0;
	}

	vec3 sunlight = EvaluateSunLight();

	// Blend the water color
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
	fragColor = texture(screenTexture, refractTargetSS);
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

	// Compute the line segment for refraction ray tracing
	vec3 reflectedVS = reflect(normalize(v_viewPosition.xyz), normalVS);
	reflectedVS = reflect(reflectedVS, waterPC.waterPlane.xyz); // reflection's Z position is inverted
	vec3 reflectTargetVS = v_viewPosition + reflectedVS * (abs(v_viewPosition.z) + 1.0);
	if (reflectTargetVS.z > -0.001) {
		reflectTargetVS = mix(v_viewPosition, reflectedVS,
			(-0.001 - v_viewPosition.z) / (reflectedVS.z - v_viewPosition.z));
	}

	vec3 reflectTargetNDC = vec3(
		reflectTargetVS.xy / reflectTargetVS.z / waterPC.fovTan.xy,
		encodeDepth(reflectTargetVS.z, waterPC.zNearFar.x, waterPC.zNearFar.y)
	);

	vec2 reflectTargetSS = reflectTargetNDC.xy * vec2(-0.5, -0.5) + 0.5;

	// Screen-space ray tracing
	targetScrPos = reflectTargetSS;
	targetDepth = reflectTargetNDC.z;
	for (float i = dither / 16.0; i <= 1.0; i += 1.0 / 16.0) {
		float rayDepth = mix(origDepth, targetDepth, i);
		reflectTargetSS = mix(origScrPos, targetScrPos, i);
		depth = texture(mirrorDepthTexture, reflectTargetSS).x;
		if (depth < rayDepth && // ray intersects the object
			depth > rayDepth - 0.1) { // (perhaps ray's actually going behind the object!)
			i = max(0.0, i - 1.0 / 16.0);
			reflectTargetSS = mix(origScrPos, targetScrPos, i);
			depth = texture(mirrorDepthTexture, reflectTargetSS).x;
			break;
		}
	}

	// convert to linear Z
	bool reflectedSky = depth > 0.99999;
	depth = decodeDepth(depth, waterPC.zNearFar.x, waterPC.zNearFar.y);

	// make sure the reflection is from above the water plane.
	// fovTan carries OpenGL's screen-Y sign, but reflectTargetSS is a Vulkan
	// texture UV (Y inverted: vk_ss.y = 1 - gl_ss.y). Reconstructing the view
	// coordinate straight from the Vulkan UV negates sampledViewCoord.y, which
	// flips the sign of the plane test's dominant term at grazing angles — so
	// validReflection wrongly failed and the reflection fell back to sky. Feed
	// the GL-convention UV (1 - y) so the test matches the OpenGL reference.
	// (Texture sampling below still uses the real Vulkan UV reflectTargetSS.)
	vec2 reflectReconSS = vec2(reflectTargetSS.x, 1.0 - reflectTargetSS.y);
	sampledViewCoord = vec3(mix(waterPC.fovTan.zw, waterPC.fovTan.xy, reflectReconSS), 1.0) * -depth;
	float planeDistance = dot(vec4(sampledViewCoord, 1.0), waterPC.waterPlane);
	bool validReflection = planeDistance > 0.0;

	vec3 reflected = texture(mirrorTexture, reflectTargetSS).xyz;

	if (!validReflection) {
		// The mirrored framebuffer isn't providing a valid reflected image.
		// Retry ray trace on the normal framebuffer

		// Compute the line segment for refraction ray tracing
		reflectedVS = reflect(normalize(v_viewPosition.xyz), normalVS);
		reflectTargetVS = v_viewPosition + reflectedVS * (abs(v_viewPosition.z) + 1.0);
		if (reflectTargetVS.z > -0.001) {
			reflectTargetVS = mix(v_viewPosition, reflectedVS,
				(-0.001 - v_viewPosition.z) / (reflectedVS.z - v_viewPosition.z));
		}
		reflectTargetNDC = vec3(
			reflectTargetVS.xy / reflectTargetVS.z / waterPC.fovTan.xy,
			encodeDepth(reflectTargetVS.z, waterPC.zNearFar.x, waterPC.zNearFar.y)
		);

		reflectTargetSS = reflectTargetNDC.xy * vec2(-0.5, -0.5) + 0.5;

		// Screen-space ray tracing
		targetScrPos = reflectTargetSS;
		targetDepth = reflectTargetNDC.z;
		for (float i = dither / 32.0; i <= 1.0; i += 1.0 / 32.0) {
			float rayDepth = mix(origDepth, targetDepth, i);
			reflectTargetSS = mix(origScrPos, targetScrPos, i);
			depth = texture(depthTexture, reflectTargetSS).x;
			if (depth < rayDepth && // ray intersects the object
				depth > rayDepth - 0.1) { // (perhaps ray's actually going behind the object!)
				//i = max(0.0, i - 1.0 / 32.0);
				//reflectTargetSS = mix(origScrPos, targetScrPos, i);
				//depth = texture(depthTexture, reflectTargetSS).x;
				break;
			}
		}

		reflectedSky = depth > 0.99999;
		reflected = texture(screenTexture, reflectTargetSS).xyz;
	}

	vec3 ongoing = normalize(worldPositionFromOrigin);

    // bluring for far surface
	float lodBias = 1.0 / ongoing.z;
	float dispScaleByLod = min(1.0, ongoing.z * 0.5);
    lodBias = log2(lodBias);
    lodBias = clamp(lodBias, 0.0, 2.0);

	// compute reflection color
	vec2 reflectionSS = origScrPos;
	disp.y = -abs(disp.y * 3.0);
	reflectionSS -= disp * scale * waterPC.displaceScale * 15.0;

	vec3 refl = reflected;
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
	if (dot(sunlight, vec3(1.0)) > 0.0001 && reflectedSky) {
		// can't use CockTorrance here -- CockTorrance's fresenel term
		// is hard-coded for higher roughness values
		vec3 lightVec = -waterPC.sunDirection.xyz * 1.4142135;
		vec3 halfVec = lightVec + ongoing;
		halfVec = (dot(halfVec, halfVec) < 0.00000000001)
			? vec3(1.0, 0.0, 0.0) : normalize(halfVec);

		float dotNL = max(dot(wave, lightVec), 0.0);
		float dotNH = max(dot(wave, halfVec), 0.00001);

		// distribution
		float m = 0.001 + 0.00015 / (abs(ongoing.z) + 0.0006); // roughness
		float spec = GGXDistribution(m, dotNH);

		// fresnel
		spec *= reflective;

		// geometric shadowing (Kelemen)
		float visibility = (dotNL*dotNV) / (dotNH*dotNH);
		spec *= max(0.0, visibility);

		// limit brightness (flickering specular reflection might cause seizure to some people)
		spec = min(spec, 120.0);

		fragColor.xyz += sunlight * spec * att;
	}

#if !LINEAR_FRAMEBUFFER
	fragColor.xyz = sqrt(fragColor.xyz);
#endif

	fragColor.w = 1.0;
}
