#extension GL_EXT_texture_array : require
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
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

varying vec3 fogDensity;
varying vec3 screenPosition;
varying vec3 viewPosition;
varying vec3 worldPosition;
varying vec2 worldPositionOriginal;

uniform sampler2D screenTexture;
uniform sampler2D depthTexture;
uniform sampler2D mainTexture;
uniform sampler2DArray waveTextureArray;
uniform sampler2D mirrorTexture;
uniform mat4 viewMatrix;
uniform vec3 fogColor;
uniform vec3 skyColor;
uniform vec2 zNearFar;
uniform vec4 fovTan;
uniform vec4 waterPlane;
uniform vec3 viewOriginVector;

uniform vec2 displaceScale;

vec3 EvaluateSunLight();
vec3 EvaluateAmbientLight(float detailAmbientOcclusion);
vec3 EvaluateDynamicLights(vec3 position, vec3 normal);
vec3 EvaluateWaterDynamicLightGlint(vec3 position, vec3 wave, vec3 ongoing);
float GGXDistribution(float m, float dotHalf);

float decodeDepth(float w, float near, float far) {
	return far * near / mix(far, near, w);
}

float depthAt(vec2 pt) {
	float w = texture2D(depthTexture, pt).x;
	return decodeDepth(w, zNearFar.x, zNearFar.y);
}

void main() {
	vec3 worldPositionFromOrigin = worldPosition - viewOriginVector;
	vec4 waveCoord = worldPositionOriginal.xyxy
		* vec4(vec2(0.08), vec2(0.15704))
		+ vec4(0., 0., 0.754, 0.1315);

	vec2 waveCoord2 = worldPositionOriginal.xy * 0.02344 + vec2(0.154, 0.7315);

	// evaluate waveform (normal vector)
	vec3 wave = texture2DArray(waveTextureArray, vec3(waveCoord.xy, 0.0)).xyz;
	wave = mix(vec3(-0.0025), vec3(0.0025), wave);
	wave.xy *= 0.08 * 0.4;

	// detail
	vec2 wave2 = texture2DArray(waveTextureArray, vec3(waveCoord.zw, 1.0)).xy;
	wave2 = mix(vec2(-0.0025), vec2(0.0025), wave2);
	wave2.xy *= 0.15704 * 0.2;
	wave.xy += wave2;

	// rough
	wave2 = texture2DArray(waveTextureArray, vec3(waveCoord2.xy, 2.0)).xy;
	wave2 = mix(vec2(-0.0025), vec2(0.0025), wave2);
	wave2.xy *= 0.02344 * 2.5;
	wave.xy += wave2;

	wave.z = (1.0 / 128.0) / 4.0; // (negated normal vector!)
	wave.xyz = normalize(wave.xyz);

	vec2 origScrPos = screenPosition.xy / screenPosition.z;
	vec2 scrPos = origScrPos;

	float scale = 1.0 / viewPosition.z;
	vec2 disp = wave.xy * 0.1;
	scrPos += disp * scale * displaceScale * 4.0;

	// check envelope length.
	// if the displaced location points the out of the water,
	// reset to the original pos.
	float depth = depthAt(scrPos);

	// convert to view coord
	vec3 sampledViewCoord = vec3(mix(fovTan.zw, fovTan.xy, scrPos), 1.0) * -depth;
	float planeDistance = dot(vec4(sampledViewCoord, 1.0), waterPlane);
	if (planeDistance < 0.0) {
		// reset!
		// original pos must be in the water.
		scrPos = origScrPos;
		depth = depthAt(scrPos);
		if (depth + viewPosition.z < 0.0) {
			// if the pixel is obscured by a object,
			// this fragment of water is not visible
			//discard; done by early-Z test
		}
	} else {
		depth = planeDistance / abs(waterPlane.z);
		depth -= viewPosition.z;
	}

	float envelope = clamp((depth + viewPosition.z), 0.0, 1.0);
	envelope = 1.0 - (1.0 - envelope) * (1.0 - envelope);
	
	vec3 diffuseShading = EvaluateAmbientLight(1.0);
	vec3 sunlight = EvaluateSunLight();
	// The surface faces up, which is -Z in world space.
	vec3 dynamicLight = EvaluateDynamicLights(worldPosition, vec3(0.0, 0.0, -1.0));

	// water color
	// TODO: correct integral
	vec2 waterCoord = worldPosition.xy;
	vec2 integralCoord = floor(waterCoord) + 0.5;
	vec2 blurDir = (worldPositionFromOrigin.xy);
	blurDir /= max(length(blurDir), 1.0);
	vec2 blurDirSign = mix(vec2(-1.0), vec2(1.0), step(0.0, blurDir));
	vec2 startPos = (waterCoord - integralCoord) * blurDirSign;
	vec2 diffPos = blurDir * envelope * blurDirSign * 0.5 /*limit blur*/;
	vec2 subCoord = 1.0 - clamp((vec2(0.5) - startPos) / diffPos, 0.0, 1.0);
	vec2 sampCoord = integralCoord + subCoord * blurDirSign;
	vec3 waterColor = texture2D(mainTexture, sampCoord / 512.0).xyz;
	waterColor *= sunlight + diffuseShading + dynamicLight;

	// underwater object color
	gl_FragColor = texture2D(screenTexture, scrPos);
#if !LINEAR_FRAMEBUFFER
	gl_FragColor.xyz *= gl_FragColor.xyz; // screen color to linear
#endif

	// apply fog color to water color now.
	// note that fog is already applied to underwater object.
	waterColor = mix(waterColor, fogColor, fogDensity);

	// blend water color with the underwater object's color.
	gl_FragColor.xyz = mix(gl_FragColor.xyz, waterColor, envelope);

	// attenuation factor for addition blendings below
	vec3 att = 1.0 - fogDensity;

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
	scrPos2 -= disp * scale * displaceScale * 15.0;

	vec3 refl = texture2D(mirrorTexture, scrPos2, lodBias).xyz;
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
	gl_FragColor.xyz = mix(gl_FragColor.xyz, refl, reflective * att);

	/* ------- Specular Reflection -------- */

	// specular reflection
	if (dot(sunlight, vec3(1.0)) > 0.0001) {
		// can't use CookTorrance here -- CookTorrance's fresenel term
		// is hard-coded for higher roughness values
		vec3 lightVec = vec3(0.0, 1.0, 1.0);
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

		gl_FragColor.xyz += sunlight * spec * att;
	}

	/* ------- Dynamic Light Glint -------- */

	gl_FragColor.xyz += EvaluateWaterDynamicLightGlint(worldPosition, wave, ongoing) * att;

#if !LINEAR_FRAMEBUFFER
	gl_FragColor.xyz = sqrt(gl_FragColor.xyz);
#endif

	gl_FragColor.w = 1.0;
}