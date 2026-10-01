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

// Common code for dynamic light rendering


// -- shadowing

float EvaluateLighting();

float VisibilityOfLight() {
	return EvaluateLighting();
}

float EvaluateDynamicLightShadow() {
	return VisibilityOfLight();
}

// -- lighting (without bumpmapping)

uniform vec3 dynamicLightColor;
uniform float dynamicLightRadius;
uniform float dynamicLightRadiusInversed;
uniform sampler2D dynamicLightProjectionTexture;

varying vec3 lightPos;
varying vec3 lightNormal;
varying vec3 lightTexCoord;

vec3 EvaluateDynamicLightNoBump() {
	// Discard fragments behind the light source
	if (lightTexCoord.z <= 0.0)
		discard;

	// Soft cone edge instead of hard discard. The projected coordinates span
	// [0, 1] with the cone axis at (0.5, 0.5), so measure from there and
	// rescale to 1.0 at the cone edge.
	vec2 coneCoord = lightTexCoord.xy / lightTexCoord.z - vec2(0.5);
	float coneDistance = length(coneCoord) * 2.0;
	// Smooth falloff at cone edge (0.8 to 1.1 normalized)
	float coneFalloff = smoothstep(1.1, 0.8, coneDistance);
	if (coneFalloff <= 0.0)
		discard;

	// diffuse lighting
	float intensity = dot(normalize(lightPos), normalize(lightNormal));
	if (intensity < 0.0)
		discard;

	// attenuation
	float distance = length(lightPos);
	if (distance >= dynamicLightRadius)
		discard;
	distance = max(1.0 - distance * dynamicLightRadiusInversed, 0.0);
	float attenuation = distance * distance;

	// apply attenuation
	intensity *= attenuation * coneFalloff;

	vec3 texValue = texture2DProj(dynamicLightProjectionTexture, lightTexCoord).xyz;

	// TODO: specular lighting?
	return dynamicLightColor * intensity * EvaluateDynamicLightShadow() * texValue;
}

// TODO: bumpmapping variant (requires tangent vector)