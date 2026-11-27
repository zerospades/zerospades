uniform mat4 projectionViewModelMatrix;
uniform mat4 modelMatrix;
uniform vec3 modelOrigin;
uniform vec2 texScale;
uniform vec3 sunLightDirection;

// [x, y, z]
attribute vec3 positionAttribute;

// [u, v]
attribute vec2 textureCoordAttribute;

// [x, y, z]
attribute vec3 normalAttribute;

varying vec4 textureCoord;
varying float flatShading;

void main() {
	vec4 vertexPos = vec4(modelOrigin + positionAttribute, 1.0);

	gl_Position = projectionViewModelMatrix * vertexPos;

	textureCoord = textureCoordAttribute.xyxy * vec4(texScale, vec2(1.0));

	// lambert reflection
	vec3 normal = normalize((modelMatrix * vec4(normalAttribute, 0.0)).xyz);
	flatShading = max(dot(normal, sunLightDirection), 0.0);
}
