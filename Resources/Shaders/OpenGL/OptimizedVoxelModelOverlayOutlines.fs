uniform float modelOpacity;

void main() {
	// Black stays black whether or not the framebuffer encodes sRGB by itself.
	gl_FragColor = vec4(0.0, 0.0, 0.0, modelOpacity);
}
