uniform mat4 projectionViewModelMatrix;
uniform vec3 modelOrigin;

// [x, y, z]
attribute vec3 positionAttribute;

void main() {
	gl_Position = projectionViewModelMatrix * vec4(modelOrigin + positionAttribute, 1.0);
}
