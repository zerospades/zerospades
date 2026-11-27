uniform mat4 projectionViewModelMatrix;
uniform mat4 modelMatrix;
uniform vec3 modelOrigin;
uniform vec2 texScale;

// [x, y, z]
attribute vec3 positionAttribute;

// [u, v]
attribute vec2 textureCoordAttribute;

// [x, y, z]
attribute vec3 normalAttribute;

varying vec4 textureCoord;
varying float flatShading;

// The normal in overlay space, where the camera looks along -Y.
varying vec3 overlayNormal;

uniform vec3 sunLightDirection;

void main() {
	vec4 vertexPos = vec4(modelOrigin + positionAttribute, 1.0);

	gl_Position = projectionViewModelMatrix * vertexPos;

	textureCoord = textureCoordAttribute.xyxy * vec4(texScale, vec2(1.0));

	overlayNormal = normalize((modelMatrix * vec4(normalAttribute, 0.0)).xyz);
	flatShading = max(dot(overlayNormal, sunLightDirection), 0.0);
}
