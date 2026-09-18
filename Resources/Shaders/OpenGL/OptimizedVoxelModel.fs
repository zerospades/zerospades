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

varying vec4 textureCoord;
varying vec3 fogDensity;
varying float flatShading;

uniform sampler2D ambientOcclusionTexture;
uniform sampler2D modelTexture;
uniform vec3 fogColor;
uniform vec3 customColor;
uniform float modelOpacity;

// 1.0 lights the model from every direction at once (the model editor), 0.0
// keeps the sun. See SceneDefinition::flatModelLighting.
uniform float flatLighting;

vec3 EvaluateSunLight();
vec3 EvaluateAmbientLight(float detailAmbientOcclusion);

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
	diffuseShading += vec3(flatShading) * EvaluateSunLight();

	// Omnidirectional: every face reaches full albedo, so one colour stays one
	// colour whichever way its faces point. Ambient occlusion is kept, being a
	// soft corner cue rather than the hard per-face step the sun term creates.
	diffuseShading = mix(diffuseShading, vec3(ao), flatLighting);

	// apply diffuse shading
	if (!isEmissive)
		gl_FragColor.xyz *= diffuseShading;

	// apply fog fading
	gl_FragColor.xyz = mix(gl_FragColor.xyz, fogColor, fogDensity);

#if !LINEAR_FRAMEBUFFER
	gl_FragColor.xyz = sqrt(gl_FragColor.xyz);
#endif

	// Only valid in the ghost pass - Blending is disabled for most models
	gl_FragColor.w = modelOpacity;
}