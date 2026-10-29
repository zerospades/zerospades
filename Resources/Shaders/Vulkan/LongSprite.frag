#version 450
#extension GL_GOOGLE_include_directive : require

// The view, from the sprites' own set (`SpriteView.glsl`), whose image this is too
#include "SpriteView.glsl"


layout(location = 0) in vec4 color;
layout(location = 1) in vec2 texCoord;
layout(location = 2) in vec4 fogDensity;

layout(location = 0) out vec4 fragColor;

void main() {
	vec4 texColor = texture(mainTexture, texCoord);

	// Linearize the sampled texel; see the note in Sprite.frag.
	texColor.xyz *= texColor.xyz;

	// Premultiplied alpha
	texColor.xyz *= texColor.w;
	texColor *= color;

	// Apply fog
	vec4 fogColorP = vec4(spriteView.fogColorDistance.xyz, 1.0);
	fogColorP *= texColor.w; // Premultiplied alpha
	texColor = mix(texColor, fogColorP, fogDensity);

	// Discard nearly transparent fragments
	if (dot(texColor, vec4(1.0)) < 0.002)
		discard;

	fragColor = texColor;
}
