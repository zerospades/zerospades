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

varying vec4 textureCoord;
varying vec3 fogDensity;
varying float flatShading;

varying vec3 viewSpaceCoord;
varying vec3 viewSpaceNormal;
varying vec3 reflectionDir;

uniform vec3 viewSpaceLight;

uniform sampler2D ambientOcclusionTexture;
uniform sampler2D modelTexture;
uniform vec3 fogColor;
uniform vec3 customColor;
uniform float modelOpacity;

// 1.0 lights the model from every direction at once (the model editor), 0.0
// keeps the sun. See SceneDefinition::flatModelLighting.
uniform float flatLighting;

float VisibilityOfSunLight();
vec3 EvaluateAmbientLight(float detailAmbientOcclusion);
vec3 EvaluateDirectionalAmbientLight(float detailAmbientOcclusion, vec3 direction);

float OrenNayar(float sigma, float dotLight, float dotEye);
float CookTorrance(vec3 eyeVec, vec3 lightVec, vec3 normal);

void main() {
	vec4 texData = texture2D(modelTexture, textureCoord.xy);

	// Emissive material flag is encoded in AOID
	bool isEmissive = (texData.w == 1.0);

	// model color
	gl_FragColor = vec4(texData.xyz, 1.0);
	if (dot(gl_FragColor.xyz, vec3(1.0)) < 0.0001)
		gl_FragColor.xyz = customColor;

	// linearize
	gl_FragColor.xyz *= gl_FragColor.xyz;

	// ambient occlusion
	float aoID = texData.w * (255.0 / 256.0);

	float aoY = aoID * 16.0;
	float aoX = fract(aoY);
	aoY = floor(aoY) / 16.0;

	vec2 ambientOcclusionCoord = vec2(aoX, aoY);
	ambientOcclusionCoord += fract(textureCoord.zw) * (15.0 / 256.0);
	ambientOcclusionCoord += 0.5 / 256.0;

	float ao = texture2D(ambientOcclusionTexture, ambientOcclusionCoord).x;
	vec3 diffuseShading = EvaluateAmbientLight(ao);
	// Omnidirectional: drop the sun so the two shading branches below fall away,
	// leaving every face at full albedo. See the matching note in
	// OptimizedVoxelModel.fs.
	float shadowing = VisibilityOfSunLight() * 0.6 * (1.0 - flatLighting);

	vec3 eyeVec = -normalize(viewSpaceCoord);
	vec3 normal = normalize(viewSpaceNormal);
	
	float dotNL = flatShading;
	float dotNV = max(dot(normal, eyeVec), 0.0);

	// fresnel term
	// FIXME: use split-sum approximation from UE4
	float fresnel2 = 1.0 - dotNV;
	// Reflections are directional too, so they are faded out along with the sun.
	float fresnel = (0.03 + 0.1 * fresnel2 * fresnel2) * (1.0 - flatLighting);

	// specular shading (blurred reflections, assuming roughness is high)
	vec3 reflectWS = normalize(reflectionDir);
	vec3 specularShading = EvaluateDirectionalAmbientLight(ao, reflectWS);

	// diffuse/specular shading for sunlight
	if (!isEmissive && shadowing > 0.0 && dotNL > 0.0) {
		float sunDiffuseShading = OrenNayar(0.8, dotNL, dotNV);
		float sunSpecularShading = CookTorrance(eyeVec, viewSpaceLight, normal);
	
		diffuseShading += sunDiffuseShading * shadowing;
		gl_FragColor.xyz += sunSpecularShading * shadowing;
	}

	diffuseShading = mix(diffuseShading, vec3(ao), flatLighting);

	// apply diffuse/specular shading
	if (!isEmissive)
		gl_FragColor.xyz = mix(diffuseShading * gl_FragColor.xyz, specularShading, fresnel);

	// apply fog fading
	gl_FragColor.xyz = mix(gl_FragColor.xyz, fogColor, fogDensity);
	gl_FragColor.xyz = max(gl_FragColor.xyz, 0.0);

	// gamma correct
#if !LINEAR_FRAMEBUFFER
	gl_FragColor.xyz = sqrt(gl_FragColor.xyz);
#endif

#if USE_HDR
	// somehow denormal occurs, so detect it here and remove
	// (denormal destroys screen)
	if (gl_FragColor.xyz != gl_FragColor.xyz)
		gl_FragColor.xyz = vec3(0.0);
#endif

	// Only valid in the ghost pass - Blending is disabled for most models
	gl_FragColor.w = modelOpacity;
}