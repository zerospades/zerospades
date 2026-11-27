varying vec4 textureCoord;
varying float flatShading;

uniform sampler2D ambientOcclusionTexture;
uniform sampler2D modelTexture;
uniform vec3 customColor;
uniform bool outputIsLinear; // 1 when the framebuffer encodes sRGB by itself
uniform float modelOpacity;

const float ambientLevel = 0.55;
const float sunLevel = 0.6;

void main() {
	vec4 texData = texture2D(modelTexture, textureCoord.xy);

	// Emissive material flag is encoded in AOID
	bool isEmissive = (texData.w == 1.0);

	// model color
	vec3 color = texData.xyz;
	if (dot(color, vec3(1.0)) < 0.0001)
		color = customColor;

	// linearize
	color *= color;

	// ambient occlusion
	float aoID = texData.w * (255.0 / 256.0);

	float aoY = aoID * 16.0;
	float aoX = fract(aoY);
	aoY = floor(aoY) / 16.0;

	vec2 ambientOcclusionCoord = vec2(aoX, aoY);
	ambientOcclusionCoord += fract(textureCoord.zw) * (15.0 / 256.0);
	ambientOcclusionCoord += 0.5 / 256.0;

	float ao = texture2D(ambientOcclusionTexture, ambientOcclusionCoord).x;

	if (!isEmissive)
		color *= vec3(ambientLevel * ao + sunLevel * flatShading);

	if (!outputIsLinear)
		color = sqrt(color);

	gl_FragColor = vec4(color, modelOpacity);
}
