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

// Common code for sunlight shadow rendering

#if USE_SSAO
layout(binding = 3) uniform sampler2D ssaoTexture;
layout(binding = 0) uniform SSAOUniforms {
	vec2 ssaoTextureUVScale;
} ssao;
#endif

float EvaluateMapShadow();
float EvaluteModelShadow();
vec3 EvaluateRadiosity(float detailAmbientOcclusion, float ssao);
vec3 EvaluateSoftReflections(float detailAmbientOcclusion, vec3 direction, float ssao);

float VisibilityOfSunLight() {
	return EvaluateMapShadow() * EvaluteModelShadow();
}

vec3 EvaluateSunLight() {
	return vec3(0.6) * VisibilityOfSunLight();
}

vec3 EvaluateAmbientLight(float detailAmbientOcclusion) {
#if USE_SSAO
    float ssaoValue = texture(ssaoTexture, gl_FragCoord.xy * ssao.ssaoTextureUVScale).x;
#else // USE_SSAO
    float ssaoValue = 1.0;
#endif // USE_SSAO
	return EvaluateRadiosity(detailAmbientOcclusion, ssaoValue);
}

vec3 EvaluateDirectionalAmbientLight(float detailAmbientOcclusion, vec3 direction) {
#if USE_SSAO
    float ssaoValue = texture(ssaoTexture, gl_FragCoord.xy * ssao.ssaoTextureUVScale).x;
#else // USE_SSAO
    float ssaoValue = 1.0;
#endif // USE_SSAO
    return EvaluateSoftReflections(detailAmbientOcclusion, direction, ssaoValue);
}
