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

layout(binding = 0) uniform sampler2D mainTexture;

layout(location = 0) in vec4 color;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 fogDensity;

layout(location = 0) out vec4 fragColor;

void main() {
	vec4 texColor = texture(mainTexture, texCoord);

	// Premultiplied alpha
	texColor.xyz *= texColor.w;
	texColor *= color;

	// Apply fog
	vec4 fogColorP = vec4(pc.fogColor, 1.0);
	fogColorP *= texColor.w; // Premultiplied alpha
	texColor = mix(texColor, fogColorP, fogDensity);

	// Discard nearly transparent fragments
	if (dot(texColor, vec4(1.0)) < 0.002)
		discard;

	fragColor = texColor;
}
