uniform vec3 xrayColor;
uniform sampler2D modelTexture;
uniform vec3 customColor;
uniform vec3 viewSpaceLight;

varying float fresnel;
varying vec2 textureCoord;
varying vec3 viewSpaceCoord;
varying vec3 viewSpaceNormal;

float CookTorrance(vec3 eyeVec, vec3 lightVec, vec3 normal);

void main() {
    vec4 texData = texture2D(modelTexture, textureCoord);

    vec3 albedo = texData.xyz;
    if (dot(albedo, vec3(1.0)) < 0.0001)
        albedo = customColor;
    albedo *= albedo; // linearize

    vec3 eyeVec = -normalize(viewSpaceCoord);
    vec3 normal = normalize(viewSpaceNormal);

    float sunSpecularShading = CookTorrance(eyeVec, viewSpaceLight, normal);

    vec3 color = xrayColor * mix(0.5 + albedo * 0.5, vec3(1.0), fresnel);
    color += xrayColor * sunSpecularShading * 4.0;

	// gamma correct
#if !LINEAR_FRAMEBUFFER
    color = sqrt(color);
#endif

    float scanline = sin(gl_FragCoord.y * 2.0) * 0.5 + 0.5;
    color *= mix(0.6, 1.0, scanline);

    float alpha = mix(0.9, 1.0, fresnel) + clamp(sunSpecularShading * 0.8, 0.0, 1.0);
    alpha = min(alpha, 1.0);

    gl_FragColor = vec4(color, alpha);
}