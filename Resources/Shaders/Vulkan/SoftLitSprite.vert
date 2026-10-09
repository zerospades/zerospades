/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#version 450
#extension GL_GOOGLE_include_directive : require

// A soft sprite lit as the volume it stands for (`r_softParticles` 2), as GL's
// SoftLitSprite.vs: its quad is brought to the front of the volume like a soft
// sprite's, and each corner gets the normal of the volume's bulge there. The
// dynamic lights are scattered through the volume at the corners, and the sun and
// the sky are taken per fragment.

#define SCENE_LIGHT_SET 2
#define SCENE_LIGHT_CLUSTER_ACCESS readonly
#define SCENE_LIGHT_VERTEX_STAGE
#include "SceneLight/Lights.glsl"
#include "SceneLight/ModelShadow.glsl"
#include "SpriteView.glsl"

// The layer of soft sprites this pipeline draws (`VulkanSpriteRenderer::Layer`)
layout(constant_id = 1) const int SPRITE_LAYER = SPRITE_LAYER_EVERY;

layout(location = 0) in vec4 centerRadiusAttribute;
layout(location = 1) in vec4 colorAttribute;
layout(location = 2) in float angleAttribute;
// 1 for a sprite that scatters the light it is lit by, 0 for one that emits its own
layout(location = 3) in float scatteringAttribute;

layout(location = 0) out vec4 color;
layout(location = 1) out vec4 texCoord;
layout(location = 2) out vec4 fogDensity;
layout(location = 3) out vec4 depthRange;
layout(location = 4) out vec3 emission;
layout(location = 5) out vec3 normal;
layout(location = 6) out vec3 dynamicLight;
layout(location = 7) out vec3 shadowCoord;
layout(location = 8) out vec3 aoCoord;
layout(location = 9) out vec3 radiosityTextureCoord;
layout(location = 10) out vec3 modelShadowCoord0;
layout(location = 11) out vec3 modelShadowCoord1;
layout(location = 12) out vec3 modelShadowCoord2;
layout(location = 13) out float shadowViewDepth;

void main() {
	vec3 center = centerRadiusAttribute.xyz;
	float radius = centerRadiusAttribute.w;
	vec2 corner = SpriteCorner(gl_VertexIndex);
	vec3 pos = SpriteCornerPosition(center, radius, angleAttribute, corner);

	vec3 eye = spriteView.eye.xyz;
	vec3 front = spriteView.front.xyz;

	// The normal of the volume's bulge, and the light where it is
	normal = normalize((pos - center) - front);
	dynamicLight = EvaluateDynamicLightsScattered(pos, normal);

	// Where it is in the map's shadow, radiosity and ambient occlusion, as on the
	// map's surfaces
	shadowCoord = vec3(pos.x / 512.0, (pos.y - pos.z) / 512.0, pos.z / 255.0);
	aoCoord = (pos + vec3(0.0, 0.0, 1.0)) / vec3(512.0, 512.0, 65.0);
	radiosityTextureCoord = pos / vec3(512.0, 512.0, 64.0);
	modelShadowCoord0 = (shadowSampling.cascadeMatrix[0] * vec4(pos, 1.0)).xyz;
	modelShadowCoord1 = (shadowSampling.cascadeMatrix[1] * vec4(pos, 1.0)).xyz;
	modelShadowCoord2 = (shadowSampling.cascadeMatrix[2] * vec4(pos, 1.0)).xyz;
	shadowViewDepth = dot(pos - eye, front);

	// An emissive sprite, drawn with no alpha and a colour, shines by itself.
	if (colorAttribute.w < 0.0001 && any(greaterThan(colorAttribute.xyz, colorAttribute.www))) {
		emission = colorAttribute.xyz;
		color = vec4(0.0);
	} else {
		emission = vec3(0.0);
		color = colorAttribute;
	}

	// Move sprite to the front of the volume
	float centerDepth = dot(center - eye, front);
	depthRange.xy = vec2(centerDepth) + vec2(-1.0, 1.0) * radius;

	// Clip the volume by the near clip plane
	float frontDepth = depthRange.x;
	frontDepth = max(frontDepth, 0.3);
	frontDepth = min(frontDepth, depthRange.y);
	depthRange.w = frontDepth;

	// Along the lines of sight, so the sprite still covers the same pixels (see
	// SoftSprite.vert)
	if (centerDepth > 0.001)
		pos = eye + (pos - eye) * (frontDepth / centerDepth);
	else
		pos += front * (frontDepth - centerDepth);

	gl_Position = spriteView.projectionView * vec4(pos, 1.0);

	// What this layer draws of it: none collapses its quad.
	float layerFade = SpriteLayerFade(center, radius, SPRITE_LAYER);
	if (layerFade <= 0.0)
		gl_Position = vec4(0.0, 0.0, 0.0, 1.0);
	color *= layerFade;
	emission *= layerFade;

	texCoord.xy = corner * 0.5 + 0.5;

	// Depth texture coord: the scene's images run from the top down.
	vec2 ndc = gl_Position.xy / gl_Position.w;
	texCoord.zw = vec2(0.5 + ndc.x * 0.5, 0.5 - ndc.y * 0.5);

	fogDensity = vec4(SpriteFogDensity(pos));

	// Precompute depth range values for fragment shader
	float nearFar = spriteView.nearFar.x * spriteView.nearFar.y;
	depthRange.z = 1.0 / (depthRange.y - depthRange.w);
	depthRange.y = depthRange.x;
	depthRange.x *= -depthRange.z;

	depthRange.y /= nearFar;
	depthRange.z *= nearFar;
}
