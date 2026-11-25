#version 450
#extension GL_GOOGLE_include_directive : require

// The view, from the sprites' own set (`SpriteView.glsl`), whose image this is too
#include "SpriteView.glsl"

layout(location = 0) in vec3 positionAttribute;
layout(location = 1) in vec2 texCoordAttribute;
layout(location = 2) in vec4 colorAttribute;

layout(location = 0) out vec4 color;
layout(location = 1) out vec2 texCoord;
layout(location = 2) out vec4 fogDensity;

void main() {
	vec3 pos = positionAttribute;
	gl_Position = spriteView.projectionView * vec4(pos, 1.0);

	color = colorAttribute;
	texCoord = texCoordAttribute;

	fogDensity = vec4(SpriteFogDensity(pos));
}
