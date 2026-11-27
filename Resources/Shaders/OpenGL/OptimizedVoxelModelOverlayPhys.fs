varying vec4 textureCoord;
varying float flatShading;
varying vec3 overlayNormal;

uniform sampler2D ambientOcclusionTexture;
uniform sampler2D modelTexture;
uniform vec3 customColor;
uniform vec3 sunLightDirection;
uniform vec3 eyeDirection;
uniform bool outputIsLinear; // 1 when the framebuffer encodes sRGB by itself
uniform float modelOpacity;

float OrenNayar(float sigma, float dotLight, float dotEye);
float CookTorrance(vec3 eyeVec, vec3 lightVec, vec3 normal);

const float ambientLevel = 0.55;
const float sunLevel = 0.6;

// Stand-in for the world's ambient light: what a reflection sees looking up
// (-Z in overlay space) and looking down.
const vec3 skyLight = vec3(1.0);
const vec3 groundLight = vec3(0.0);

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

	vec3 normal = normalize(overlayNormal);

	float dotNL = flatShading;
	float dotNV = max(dot(normal, eyeDirection), 0.0);

	// fresnel term
	float fresnel2 = 1.0 - dotNV;
	float fresnel = 0.03 + 0.1 * fresnel2 * fresnel2;

	// diffuse shading from the ambient light
	vec3 diffuseShading = vec3(ambientLevel * ao);

	// specular shading (blurred reflections, assuming roughness is high)
	vec3 reflectDir = reflect(-eyeDirection, normal);
	float skyFactor = clamp(0.5 - 0.5 * reflectDir.z, 0.0, 1.0);
	vec3 specularShading = mix(groundLight, skyLight, skyFactor) * ao;

	// diffuse/specular shading for the sunlight
	if (!isEmissive && dotNL > 0.0) {
		float sunDiffuseShading = OrenNayar(0.8, dotNL, dotNV);
		float sunSpecularShading = CookTorrance(eyeDirection, sunLightDirection, normal);

		diffuseShading += sunDiffuseShading * sunLevel;
		color += sunSpecularShading * sunLevel;
	}

	// apply diffuse/specular shading
	if (!isEmissive)
		color = mix(diffuseShading * color, specularShading, fresnel);

	color = max(color, 0.0);

	if (!outputIsLinear)
		color = sqrt(color);

	gl_FragColor = vec4(color, modelOpacity);
}
