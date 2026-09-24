#version 450

layout(push_constant) uniform PushConstants {
	mat4 projectionViewMatrix;
	mat4 viewMatrix;
	vec3 rightVector;
	vec3 upVector;
	vec3 viewOriginVector;
	vec3 fogColor;
	float fogDistance;
} pc;

layout(location = 0) in vec3 positionAttribute;
layout(location = 1) in vec2 texCoordAttribute;
layout(location = 2) in vec4 colorAttribute;

layout(location = 0) out vec4 color;
layout(location = 1) out vec2 texCoord;
layout(location = 2) out vec4 fogDensity;

void main() {
	vec3 pos = positionAttribute;
	gl_Position = pc.projectionViewMatrix * vec4(pos, 1.0);

	color = colorAttribute;
	texCoord = texCoordAttribute;

	// Fog calculation
	vec2 horzRelativePos = pos.xy - pc.viewOriginVector.xy;
	float horzDistance = dot(horzRelativePos, horzRelativePos);
	float density = clamp(horzDistance / (pc.fogDistance * pc.fogDistance), 0.0, 1.0);
	fogDensity = vec4(density);
}
