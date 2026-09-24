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

#version 450

layout(push_constant) uniform PushConstants {
	mat4 projectionViewModelMatrix;
	mat4 modelMatrix;
	vec3 modelOrigin;
	float fogDensity;
	vec3 customColor;
	float lightRadius;
	vec3 lightOrigin;
	float lightType;
	vec3 lightColor;
	float lightRadiusInversed;
	vec3 lightLinearDirection;
	float lightLinearLength;
	mat4 lightSpotMatrix; // unused here; kept to match the vertex push block
} pc;

layout(location = 0) in vec4 color;
layout(location = 1) in vec3 lightPos;
layout(location = 2) in vec3 lightNormal;
layout(location = 3) in float fogDensityIn;
layout(location = 4) in vec3 lightTexCoord;

// Spotlight projection cookie (Gfx/Spotlight.jpg). Bound for every light; only
// sampled for spotlights, so point/linear lights get the 1x1 white fallback.
layout(set = 0, binding = 0) uniform sampler2D spotCookie;

layout(location = 0) out vec4 fragColor;

void main() {
	// Spotlight projection check + projected cookie (matches GL texture2DProj).
	vec3 cookie = vec3(1.0);
	if (pc.lightType == 2.0) {
		if (lightTexCoord.z < 0.0 ||
		    any(lessThan(lightTexCoord.xy, vec2(0.0))) ||
		    any(greaterThan(lightTexCoord.xy, vec2(lightTexCoord.z))))
			discard;
		cookie = textureProj(spotCookie, lightTexCoord).xyz;
	}

	// Diffuse lighting
	float intensity = dot(normalize(lightPos), normalize(lightNormal));
	if (intensity < 0.0)
		discard;

	// Distance attenuation
	float dist = length(lightPos);
	if (dist >= pc.lightRadius)
		discard;
	float att = max(1.0 - dist * pc.lightRadiusInversed, 0.0);
	float attenuation = att * att;

	intensity *= attenuation;

	// Output: surface color * light contribution * projected cookie
	fragColor = vec4(color.xyz, 1.0);
	fragColor.xyz *= pc.lightColor * intensity * cookie;

	// Fog fading (fade to black since this is additive)
	fragColor.xyz = mix(fragColor.xyz, vec3(0.0), fogDensityIn);
}
