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

// Common code for sunlight shadow rendering

#if USE_SSAO
uniform sampler2D ssaoTexture;
uniform vec2 ssaoTextureUVScale;
#endif

// The factor the sun's light is drawn with; at night there is none, and the sun casts
// no light and no shadow. The sky's light is drawn at the daylight by the radiosity
// evaluators.
uniform float sunlight;

float EvaluateMapShadow();
float EvaluteModelShadow();
vec3 EvaluateRadiosity(float detailAmbientOcclusion, float ssao);
vec3 EvaluateSoftReflections(float detailAmbientOcclusion, vec3 direction, float ssao);

float VisibilityOfSunLight() {
	return EvaluateMapShadow() * EvaluteModelShadow();
}

vec3 EvaluateSunLight() {
	return vec3(0.6 * sunlight) * VisibilityOfSunLight();
}

vec3 EvaluateAmbientLight(float detailAmbientOcclusion) {
#if USE_SSAO
    float ssao = texture2D(ssaoTexture, gl_FragCoord.xy * ssaoTextureUVScale).x;
#else // USE_SSAO
    float ssao = 1.0;
#endif // USE_SSAO
	return EvaluateRadiosity(detailAmbientOcclusion, ssao);
}

vec3 EvaluateDirectionalAmbientLight(float detailAmbientOcclusion, vec3 direction) {
#if USE_SSAO
    float ssao = texture2D(ssaoTexture, gl_FragCoord.xy * ssaoTextureUVScale).x;
#else // USE_SSAO
    float ssao = 1.0;
#endif // USE_SSAO
    return EvaluateSoftReflections(detailAmbientOcclusion, direction, ssao);
}