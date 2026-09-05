uniform mat4 projectionViewModelMatrix;
uniform mat4 modelMatrix;
uniform mat4 viewMatrix;
uniform mat4 viewModelMatrix;
uniform vec3 modelOrigin;
uniform vec3 viewOriginVector;
uniform vec2 texScale;

attribute vec4 positionAttribute;
attribute vec3 normalAttribute;
attribute vec2 textureCoordAttribute;

varying float fresnel;
varying vec2 textureCoord;
varying vec3 viewSpaceCoord;
varying vec3 viewSpaceNormal;

void main() {
    vec4 vertexPos = vec4(modelOrigin + positionAttribute.xyz, 1.0);
    gl_Position = projectionViewModelMatrix * vertexPos;

    vec3 worldNormal = normalize((modelMatrix * vec4(normalAttribute, 0.0)).xyz);
    vec3 worldPos = (modelMatrix * vertexPos).xyz;
    vec3 viewDir = normalize(viewOriginVector - worldPos);

    float dotNV = max(dot(worldNormal, viewDir), 0.0);
    float f0 = 0.04;
    fresnel = f0 + (1.0 - f0) * pow(1.0 - dotNV, 4.0);

    viewSpaceCoord = (viewModelMatrix * vertexPos).xyz;
    viewSpaceNormal = (viewMatrix * vec4(worldNormal, 0.0)).xyz;

    textureCoord = textureCoordAttribute * texScale;
}